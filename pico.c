#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "stringbuffer.h"
#include "terminal.h"

#define PICO_VERSION "0.0.1"
#define EDITOR_ROW_DECORATOR "~"
#define EDITOR_ROW_DECORATOR_LEN 1
#define ESC_CHAR '\x1b'
#define CTRL_KEY(k) ((k)&0x1f)

enum EditorKey
{
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

struct EditorConfig
{
    struct termios origTermios;
    int screenRows;
    int screenCols;
    int cursorX;
    int cursorY;
};

struct EditorConfig editorConfig;

static void initEditor();
static int editorReadKey();
static void editorRefreshScreen();
static void editorProcessKeyPress();
static void editorDrawRows(StringBuffer *sb);
static void editorMoveCursor(int key);
static void centerText(StringBuffer *sb, const char *text, int len);

static void die(const char *message)
{
    clearScreeen();

    perror(message);
    exit(1);
}

static void resetTerminal()
{
    if (disableRawMode(&editorConfig.origTermios) != 0)
        die("disableRawMode");
}

static void centerText(StringBuffer *sb, const char *text, int len)
{
    if (len > editorConfig.screenCols)
        len = editorConfig.screenCols;

    int centerPadding = (editorConfig.screenCols - len) / 2;

    if (centerPadding > 0)
    {
        sbAppend(sb, EDITOR_ROW_DECORATOR, EDITOR_ROW_DECORATOR_LEN);

        while (--centerPadding)
            sbAppend(sb, " ", 1);
    }

    sbAppend(sb, text, len);
}

static void editorDrawWelcome(StringBuffer *sb)
{
    const char *TITLE = "PICO editor";
    centerText(sb, TITLE, strlen(TITLE));
    sbAppend(sb, "\r\n", 2);

    char version[40] = "version ";
    strcat(version, PICO_VERSION);
    centerText(sb, version, strlen(version));
}

static void initEditor()
{
    editorConfig.cursorX = 0;
    editorConfig.cursorY = 0;

    if (getWindowSize(&editorConfig.screenRows, &editorConfig.screenCols) == -1)
        die("getWindowSize");
}

static int editorReadKey()
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == ESC_CHAR)
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return ESC_CHAR;
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return ESC_CHAR;

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return ESC_CHAR;

                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        else
        {
            return ESC_CHAR;
        }
    }

    return c;
}

static void editorRefreshScreen()
{
    StringBuffer sb = SB_INIT;

    clearScreeen();
    editorDrawRows(&sb);

    char cursorBuf[32];
    snprintf(cursorBuf, sizeof(cursorBuf), "\x1b[%d;%dH", editorConfig.cursorY + 1, editorConfig.cursorX + 1);
    sbAppend(&sb, cursorBuf, strlen(cursorBuf));

    write(STDOUT_FILENO, sb.s, sb.len);
    sbFree(&sb);
}

static void editorProcessKeyPress()
{
    int c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        clearScreeen();
        exit(0);
        break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
    case PAGE_UP:
    case PAGE_DOWN:
    case HOME_KEY:
    case END_KEY:
        editorMoveCursor(c);
        break;
    }
}

static void editorDrawRows(StringBuffer *sb)
{
    for (int i = 0; i < editorConfig.screenRows; i++)
    {
        if (i == editorConfig.screenRows / 3)
        {
            editorDrawWelcome(sb);
        }
        else
        {
            sbAppend(sb, EDITOR_ROW_DECORATOR, EDITOR_ROW_DECORATOR_LEN);
        }

        sbAppend(sb, "\x1b[K", 3);

        if (i < editorConfig.screenRows - 1)
            sbAppend(sb, "\r\n", 2);
    }
}

static void editorMoveCursor(int key)
{
    switch (key)
    {
    case ARROW_LEFT:
        if (editorConfig.cursorX > 0)
            editorConfig.cursorX--;
        break;
    case ARROW_DOWN:
        if (editorConfig.cursorY < editorConfig.screenRows)
            editorConfig.cursorY++;
        break;
    case ARROW_RIGHT:
        if (editorConfig.cursorX < editorConfig.screenCols)
            editorConfig.cursorX++;
        break;
    case ARROW_UP:
        if (editorConfig.cursorY > 0)
            editorConfig.cursorY--;
        break;
    case PAGE_UP:
    case PAGE_DOWN:
        for (int i = 0; i < editorConfig.screenRows; i++)
            editorMoveCursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        break;
    case HOME_KEY:
        editorConfig.cursorX = 0;
        break;
    case END_KEY:
        editorConfig.cursorX = editorConfig.screenCols - 1;
        break;
    }
}

int main()
{
    if (enableRawMode(&editorConfig.origTermios) != 0)
        die("enableRawMode");

    atexit(resetTerminal);
    initEditor();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeyPress();
    }

    return 0;
}
