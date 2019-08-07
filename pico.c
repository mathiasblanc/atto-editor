#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

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

typedef struct TextRow
{
    int len;
    char *text;
} TextRow;

typedef struct Document
{
    int numRows;
    TextRow *rows;
    int rowOffset;
    int colOffset;
} Document;

typedef struct EditorConfig
{
    struct termios origTermios;
    int screenRows;
    int screenCols;
    int cursorX;
    int cursorY;
} EditorConfig;

EditorConfig config;
Document document;

static int editorReadKey();
static void die(const char *message);
static void initEditor();
static void editorRefreshScreen();
static void editorProcessKeyPress();
static void editorDrawRows(StringBuffer *sb);
static void editorMoveCursor(int key);
static void centerText(StringBuffer *sb, const char *text, int len);
static void editorOpen(const char *filename);
static void editorAppendRow(const char *s, size_t len);
static void editorScroll();

static void die(const char *message)
{
    clearScreeen();

    perror(message);
    exit(1);
}

static void resetTerminal()
{
    if (disableRawMode(&config.origTermios) != 0)
        die("disableRawMode");
}

static void centerText(StringBuffer *sb, const char *text, int len)
{
    if (len > config.screenCols)
        len = config.screenCols;

    int centerPadding = (config.screenCols - len) / 2;

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
    config.cursorX = 0;
    config.cursorY = 0;

    if (getWindowSize(&config.screenRows, &config.screenCols) == -1)
        die("getWindowSize");

    document.numRows = 0;
    document.rows = NULL;
    document.rowOffset = 0;
    document.colOffset = 0;
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

static void editorScroll()
{
    if (config.cursorX < document.colOffset)
        document.colOffset = config.cursorX;

    if (config.cursorX >= document.colOffset + config.screenCols)
        document.colOffset = config.cursorX - config.screenCols + 1;

    if (config.cursorY < document.rowOffset)
        document.rowOffset = config.cursorY;

    if (config.cursorY >= document.rowOffset + config.screenRows)
        document.rowOffset = config.cursorY - config.screenRows + 1;
}

static void editorRefreshScreen()
{
    editorScroll();

    StringBuffer sb = SB_INIT;

    clearScreeen();
    editorDrawRows(&sb);

    char cursorBuf[32];
    snprintf(cursorBuf, sizeof(cursorBuf), "\x1b[%d;%dH",
             (config.cursorY - document.rowOffset) + 1,
             (config.cursorX - document.colOffset) + 1);

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

static void editorAppendRow(const char *s, size_t len)
{
    document.rows = realloc(document.rows, sizeof(TextRow) * (document.numRows + 1));
    const int at = document.numRows;

    document.rows[at].len = len;
    document.rows[at].text = malloc(len + 1);
    memcpy(document.rows[at].text, s, len);
    document.rows[at].text[len] = '\0';
    document.numRows++;
}

static void editorOpen(const char *filename)
{
    FILE *fp = fopen(filename, "r");

    if (!fp)
        die("fopen");

    char *line = NULL;
    size_t lineCap = 0;
    ssize_t len;

    while ((len = getline(&line, &lineCap, fp)) != -1)
    {
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
            len--;

        editorAppendRow(line, len);
    }

    free(line);
    fclose(fp);
}

static void editorDrawRows(StringBuffer *sb)
{
    for (int i = 0; i < config.screenRows; i++)
    {
        int documentRow = document.rowOffset + i;

        if (documentRow >= document.numRows)
        {
            if (document.numRows == 0 && i == config.screenRows / 3)
                editorDrawWelcome(sb);
            else
                sbAppend(sb, EDITOR_ROW_DECORATOR, EDITOR_ROW_DECORATOR_LEN);
        }
        else
        {
            int len = document.rows[documentRow].len - document.colOffset;

            if (len < 0)
                len = 0;

            if (len >= config.screenCols)
                len = config.screenCols;

            sbAppend(sb, &document.rows[documentRow].text[document.colOffset], len);
        }

        // erase all char from active position to the end of the screen
        sbAppend(sb, "\x1b[K", 3);

        if (i < config.screenRows - 1)
            sbAppend(sb, "\r\n", 2);
    }
}

static void editorMoveCursor(int key)
{
    TextRow *row = config.cursorY >= document.numRows ? NULL : &document.rows[config.cursorY];

    switch (key)
    {
    case ARROW_LEFT:
        if (config.cursorX > 0)
        {
            config.cursorX--;
        }
        else if (config.cursorY > 0)
        {
            config.cursorY--;
            config.cursorX = document.rows[config.cursorY].len;
        }
        break;
    case ARROW_DOWN:
        if (config.cursorY < document.numRows)
            config.cursorY++;
        break;
    case ARROW_RIGHT:
        if (row && config.cursorX < row->len)
        {
            config.cursorX++;
        }
        else if (row && config.cursorX == row->len)
        {
            config.cursorY++;
            config.cursorX = 0;
        }

        break;
    case ARROW_UP:
        if (config.cursorY > 0)
            config.cursorY--;
        break;
    case PAGE_UP:
    case PAGE_DOWN:
        for (int i = 0; i < config.screenRows; i++)
            editorMoveCursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        break;
    case HOME_KEY:
        config.cursorX = 0;
        break;
    case END_KEY:
        config.cursorX = config.screenCols - 1;
        break;
    }

    // reset cursor to the end of a line when going far right and down to a shorter line
    row = config.cursorY >= document.numRows ? NULL : &document.rows[config.cursorY];
    int rowLen = row ? row->len : 0;

    if (config.cursorX > rowLen)
        config.cursorX = rowLen;
}

int main(int argc, char *argv[])
{
    if (enableRawMode(&config.origTermios) != 0)
        die("enableRawMode");

    atexit(resetTerminal);
    initEditor();

    if (argc >= 2)
        editorOpen(argv[1]);

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeyPress();
    }

    return 0;
}
