#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "pico.h"
#include "stringbuffer.h"

#define CTRL_KEY(k) ((k)&0x1f)

// @see https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html

struct EditorConfig
{
    struct termios origTermios;
    int screenRows;
    int screenCols;
};

struct EditorConfig editorConfig;

void die(const char *message)
{
    clearScreeen();

    perror(message);
    exit(1);
}

/* 
* Reset terminal settings to their original values
*/
void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editorConfig.origTermios) == -1)
        die("tcsetattr");
}

/* 
* Put terminal into raw mode : 
* disable canonical mode -> input is read byte by byte instead of line by line
* disable ctrl+* keys
* disable OPOST (output processing) -> now requires explicit \r before \n
*/
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &editorConfig.origTermios) == -1)
        die("tcgetattr");

    atexit(disableRawMode);

    struct termios raw = editorConfig.origTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcgetattr");
}

/*
* Use ioctl with TIOCGWINSZ flag to retrieve the terminals number of rows and cols.
* We use a fallback in case ioctl fails on some systems by moving the cursor to the bottom
* right corner of the screen and retrieve the cursor position.
*/
int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // move cursor forward (C) and down (B) by 999 unit. C and B prevent
        // the cursor from going past the screen edges.
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return getCursorPosition(rows, cols);
    }
    else
    {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }

    return 0;
}

/*
* The 'n' command requests and reports the general status of the VT100. 
* Parameter 6 reports the active position of the cursor.
* The x and y coordinates are read from stdin in the form 27[30;211R
 */
int getCursorPosition(int *rows, int *cols)
{
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    char buf[16];
    unsigned int i = 0;

    while (i < sizeof(buf))
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;

        if (buf[i] == 'R')
            break;

        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

/*
* We use VT100 escape char 0x1b (27) followed by a [ and one or two more bytes 
* depending on the sequence to clear part of the screen and move the cursor.

* @See VT100 command set https://vt100.net/docs/vt100-ug/chapter3.html#ED
 */
void clearScreeen()
{
    // clear the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // reposition the cursor to the top left corner
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void initEditor()
{
    if (getWindowSize(&editorConfig.screenRows, &editorConfig.screenCols) == -1)
        die("getWindowSize");
}

char editorReadKey()
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    return c;
}

void editorRefreshScreen()
{
    StringBuffer sb = SB_INIT;

    clearScreeen();
    editorDrawRows(&sb);
    sbAppend(&sb, "\x1b[H", 3);

    write(STDOUT_FILENO, sb.s, sb.len);
    sbFree(&sb);
}

void editorProcessKeyPress()
{
    char c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        clearScreeen();
        exit(0);
        break;
    }
}

void editorDrawRows(StringBuffer *sb)
{
    for (int i = 0; i < editorConfig.screenRows; i++)
    {
        if (i == editorConfig.screenRows / 3)
        {
            char welcome[80];
            int welcomeLen = snprintf(welcome, sizeof(welcome), "PICO editor -- version %s", PICO_VERSION);

            if (welcomeLen > editorConfig.screenCols)
                welcomeLen = editorConfig.screenCols;

            int centerPadding = (editorConfig.screenCols - welcomeLen) / 2;

            if (centerPadding)
            {
                sbAppend(sb, EDITOR_ROW_DECORATOR, EDITOR_ROW_DECORATOR_LEN);

                while (--centerPadding)
                    sbAppend(sb, " ", 1);
            }

            sbAppend(sb, welcome, welcomeLen);
        }
        else
        {
            sbAppend(sb, "~", 1);
        }

        sbAppend(sb, "\x1b[K", 3);

        if (i < editorConfig.screenRows - 1)
            sbAppend(sb, "\r\n", 2);
    }
}

int main()
{
    enableRawMode();
    initEditor();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeyPress();
    }

    return 0;
}
