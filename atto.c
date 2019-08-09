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
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>
#include <ctype.h>

#include "stringbuffer.h"
#include "terminal.h"

#define ATTO_VERSION "0.0.1"
#define EDITOR_ROW_DECORATOR "~"
#define EDITOR_ROW_DECORATOR_LEN 1
#define ESC_CHAR '\x1b'
#define CTRL_KEY(k) ((k)&0x1f)
#define TAB_STOP 8
#define QUIT_TIMES 2

enum EditorKey
{
    BACKSPACE = 127,
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
    int renderLen;
    char *render;
} TextRow;

typedef struct Document
{
    int rowsCount;
    TextRow *rows;
    int rowOffset;
    int colOffset;
    char *filename;
    int dirty;
} Document;

typedef struct EditorConfig
{
    struct termios origTermios;
    int screenRows;
    int screenCols;
    int cursorX;
    int cursorY;
    int cursorRenderX;
    char statusMessage[80];
    time_t statusMessageTime;
} EditorConfig;

EditorConfig config;
Document document;

static int editorReadKey();
static void die(const char *message);
static void initEditor();
static void editorRefreshScreen();
static void editorProcessKeyPress();
static void editorUpdateRow(TextRow *row);
static void editorDrawRows(StringBuffer *sb);
static void editorMoveCursor(int key);
static void centerText(StringBuffer *sb, const char *text, int len);
static void editorOpen(const char *filename);
static void editorInsertRow(const int at, const char *s, size_t len);
static void editorScroll();
static int editorCursorXToCursorRenderX(const TextRow *row, int cursorX);
static void editorDrawStatusBar(StringBuffer *sb);
static void editorDrawMessageBar(StringBuffer *sb);
static void editorSetStatusMessage(const char *fmt, ...);
static void editorInsertCharAtRow(const char c, int at, TextRow *row);
static void editorInsertChar(const char c);
static char *editorRowsToString(int *bufferLen);
static void editorSave();
static void editorDelCharAtRow(const int at, TextRow *row);
static void editorDelChar();
static void editorFreeRow(TextRow *row);
static void editorDelRow(const int at);
static void editorAppendStringToRow(const char *s, const size_t len, TextRow *row);
static void editorInsertNewLine();
static char *editorPrompt(const char *prompt);

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

static void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(config.statusMessage, sizeof(config.statusMessage), fmt, ap);
    va_end(ap);
    config.statusMessageTime = time(NULL);
}

static void editorDrawMessageBar(StringBuffer *sb)
{
    sbAppend(sb, "\x1b[K", 3);

    int len = strlen(config.statusMessage);

    if (len > config.screenCols)
        len = config.screenCols;

    if (len && time(NULL) - config.statusMessageTime < 5)
        sbAppend(sb, config.statusMessage, len);
}

static void editorDrawStatusBar(StringBuffer *sb)
{
    sbAppend(sb, "\x1b[7m", 4);

    char status[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       document.filename ? document.filename : "[NO NAME]",
                       document.rowsCount,
                       document.dirty ? "(modified)" : "");

    char rStatus[80];
    int rLen = snprintf(rStatus, sizeof(rStatus), "%d/%d", config.cursorY + 1, document.rowsCount);

    if (len > config.screenCols)
        len = config.screenCols;

    sbAppend(sb, status, len);

    while (len < config.screenCols)
    {
        if (config.screenCols - len == rLen)
        {
            sbAppend(sb, rStatus, rLen);
            break;
        }
        else
        {
            sbAppend(sb, " ", 1);
            len++;
        }
    }

    sbAppend(sb, "\x1b[m", 3);
    sbAppend(sb, "\r\n", 2);
}

static void editorDrawWelcome(StringBuffer *sb)
{
    const char *TITLE = "ATTO editor";
    centerText(sb, TITLE, strlen(TITLE));
    sbAppend(sb, "\r\n", 2);

    char version[40] = "version ";
    strcat(version, ATTO_VERSION);
    centerText(sb, version, strlen(version));
}

static void initEditor()
{
    config.cursorX = 0;
    config.cursorY = 0;
    config.cursorRenderX = 0;
    config.statusMessage[0] = '\0';
    config.statusMessageTime = 0;

    if (getWindowSize(&config.screenRows, &config.screenCols) == -1)
        die("getWindowSize");

    //keep room for a status bar and a status message
    config.screenRows -= 2;

    document.rowsCount = 0;
    document.rows = NULL;
    document.rowOffset = 0;
    document.colOffset = 0;
    document.filename = NULL;
    document.dirty = 0;
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
    config.cursorRenderX = 0;

    if (config.cursorY < document.rowsCount)
        config.cursorRenderX = editorCursorXToCursorRenderX(
            &document.rows[config.cursorY], config.cursorX);

    if (config.cursorRenderX < document.colOffset)
        document.colOffset = config.cursorRenderX;

    if (config.cursorRenderX >= document.colOffset + config.screenCols)
        document.colOffset = config.cursorRenderX - config.screenCols + 1;

    if (config.cursorY < document.rowOffset)
        document.rowOffset = config.cursorY;

    if (config.cursorY >= document.rowOffset + config.screenRows)
        document.rowOffset = config.cursorY - config.screenRows + 1;
}

static int editorCursorXToCursorRenderX(const TextRow *row, int cursorX)
{
    int cursorRenderX = 0;

    for (int i = 0; i < cursorX; i++)
    {
        if (row->text[i] == '\t')
            cursorRenderX += (TAB_STOP - 1) - (cursorRenderX % TAB_STOP);

        cursorRenderX++;
    }

    return cursorRenderX;
}

static void editorRefreshScreen()
{
    editorScroll();

    StringBuffer sb = SB_INIT;

    clearScreeen();
    editorDrawRows(&sb);
    editorDrawStatusBar(&sb);
    editorDrawMessageBar(&sb);

    char cursorBuf[32];
    snprintf(cursorBuf, sizeof(cursorBuf), "\x1b[%d;%dH",
             (config.cursorY - document.rowOffset) + 1,
             (config.cursorRenderX - document.colOffset) + 1);

    sbAppend(&sb, cursorBuf, strlen(cursorBuf));
    write(STDOUT_FILENO, sb.s, sb.len);
    sbFree(&sb);
}

static void editorInsertCharAtRow(const char c, int at, TextRow *row)
{
    if (at < 0 || at > row->len)
        at = row->len;

    row->text = realloc(row->text, row->len + 2);
    memmove(&row->text[at + 1], &row->text[at], row->len - at + 1);
    row->len++;
    row->text[at] = c;

    editorUpdateRow(row);
    document.dirty++;
}

static void editorDelCharAtRow(const int at, TextRow *row)
{
    if (at < 0 || at > row->len)
        return;

    memmove(&row->text[at], &row->text[at + 1], row->len - at);
    row->len--;

    editorUpdateRow(row);
    document.dirty++;
}

static void editorDelChar()
{
    if (config.cursorY == document.rowsCount)
        return;

    if (config.cursorX == 0 && config.cursorY == 0)
        return;

    TextRow *row = &document.rows[config.cursorY];

    if (config.cursorX > 0)
    {
        editorDelCharAtRow(config.cursorX, row);
        config.cursorX--;
    }
    else
    {
        config.cursorX = document.rows[config.cursorY - 1].len;
        editorAppendStringToRow(row->text, row->len, &document.rows[config.cursorY - 1]);
        editorDelRow(config.cursorY);
        config.cursorY--;
    }
}

static void editorFreeRow(TextRow *row)
{
    free(row->render);
    free(row->text);
}

static void editorDelRow(const int at)
{
    if (at < 0 || at > document.rowsCount)
        return;

    editorFreeRow(&document.rows[at]);
    memmove(&document.rows[at],
            &document.rows[at + 1],
            sizeof(TextRow) * (document.rowsCount - at + 1));

    document.rowsCount--;
    document.dirty++;
}

static void editorAppendStringToRow(const char *s, const size_t len, TextRow *row)
{
    row->text = realloc(row->text, row->len + len + 1);
    memcpy(&row->text[row->len], s, len);
    row->len += len;
    row->text[row->len] = '\0';

    editorUpdateRow(row);
    document.dirty++;
}

static void editorUpdateRow(TextRow *row)
{
    int tabs = 0;
    for (int i = 0; i < row->len; i++)
        if (row->text[i] == '\t')
            tabs++;

    free(row->render);
    //TAB_STOP - 1 because \t already counts for 1
    row->render = malloc(row->len + 1 + tabs * (TAB_STOP - 1));

    int pos = 0;

    for (int i = 0; i < row->len; i++)
    {
        if (row->text[i] == '\t')
        {
            row->render[pos++] = ' ';

            while (pos % TAB_STOP != 0)
                row->render[pos++] = ' ';
        }
        else
        {
            row->render[pos++] = row->text[i];
        }
    }

    row->render[pos] = '\0';
    row->renderLen = pos;
}

static void editorInsertNewLine()
{
    if (config.cursorX == 0)
    {
        editorInsertRow(config.cursorY, "", 0);
    }
    else
    {
        TextRow *row = &document.rows[config.cursorY];
        editorInsertRow(config.cursorY + 1, &row->text[config.cursorX], row->len - config.cursorX);
        row = &document.rows[config.cursorY];
        row->len = config.cursorX;
        row->text[row->len] = '\0';
        editorUpdateRow(row);
    }

    config.cursorX = 0;
    config.cursorY++;
}

static void editorInsertRow(const int at, const char *s, size_t len)
{
    if (at < 0 || at > document.rowsCount)
        return;

    document.rows = realloc(document.rows, sizeof(TextRow) * (document.rowsCount + 1));
    memmove(&document.rows[at + 1], &document.rows[at], sizeof(TextRow) * (document.rowsCount - at));

    document.rows[at].len = len;
    document.rows[at].text = malloc(len + 1);
    memcpy(document.rows[at].text, s, len);
    document.rows[at].text[len] = '\0';

    document.rows[at].renderLen = 0;
    document.rows[at].render = NULL;
    editorUpdateRow(&document.rows[at]);

    document.rowsCount++;
    document.dirty++;
}

// caller is responsible for freeing the returned buffer
static char *editorRowsToString(int *bufferLen)
{
    int totLen = 0;

    for (int i = 0; i < document.rowsCount; i++)
        totLen += document.rows[i].len + 1;

    *bufferLen = totLen;

    char *buffer = malloc(totLen);
    char *endLine = buffer;

    for (int i = 0; i < document.rowsCount; i++)
    {
        memcpy(endLine, document.rows[i].text, document.rows[i].len);
        endLine += document.rows[i].len;
        *endLine = '\n';
        endLine++;
    }

    return buffer;
}

/*
* Improve by saving to a temporary file and renaming it 
* if the whole process succeeded without error
*/
static void editorSave()
{
    if (document.filename == NULL)
    {
        document.filename = editorPrompt("Save as : %s");

        if (document.filename == NULL)
        {
            editorSetStatusMessage("Save aborted!");
            return;
        }
    }

    int len;
    char *buffer = editorRowsToString(&len);

    int fd = open(document.filename, O_RDWR | O_CREAT, 0644);

    if (fd != -1)
    {
        if (ftruncate(fd, len) != -1)
        {
            if (write(fd, buffer, len) == len)
            {
                close(fd);
                free(buffer);

                document.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);

                return;
            }
        }

        close(fd);
    }

    free(buffer);
    editorSetStatusMessage("File NOT save! I/O error: %s", strerror(errno));
}

static void editorOpen(const char *filename)
{
    free(document.filename);
    document.filename = strdup(filename);

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

        editorInsertRow(document.rowsCount, line, len);
    }

    free(line);
    fclose(fp);
    document.dirty = 0;
}

static void editorDrawRows(StringBuffer *sb)
{
    for (int i = 0; i < config.screenRows; i++)
    {
        int documentRow = document.rowOffset + i;

        if (documentRow >= document.rowsCount)
        {
            if (document.rowsCount == 0 && i == config.screenRows / 3)
                editorDrawWelcome(sb);
            else
                sbAppend(sb, EDITOR_ROW_DECORATOR, EDITOR_ROW_DECORATOR_LEN);
        }
        else
        {
            int len = document.rows[documentRow].renderLen - document.colOffset;

            if (len < 0)
                len = 0;

            if (len >= config.screenCols)
                len = config.screenCols;

            sbAppend(sb, &document.rows[documentRow].render[document.colOffset], len);
        }

        // erase all char from active position to the end of the screen
        sbAppend(sb, "\x1b[K", 3);

        sbAppend(sb, "\r\n", 2);
    }
}

static void editorInsertChar(const char c)
{
    if (config.cursorY == document.rowsCount)
        editorInsertRow(document.rowsCount, "", 0);

    editorInsertCharAtRow(c, config.cursorX, &document.rows[config.cursorY]);
    config.cursorX++;
}

static void editorMoveCursor(int key)
{
    TextRow *row = config.cursorY >= document.rowsCount ? NULL : &document.rows[config.cursorY];

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
        if (config.cursorY < document.rowsCount)
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
        if (key == PAGE_UP)
        {
            config.cursorY = document.rowOffset;
        }
        else if (key == PAGE_DOWN)
        {
            config.cursorY = document.rowOffset + config.screenRows - 1;

            if (config.cursorY > document.rowsCount)
                config.cursorY = document.rowsCount;
        }

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
    row = config.cursorY >= document.rowsCount ? NULL : &document.rows[config.cursorY];
    int rowLen = row ? row->len : 0;

    if (config.cursorX > rowLen)
        config.cursorX = rowLen;
}

static void editorProcessKeyPress()
{
    int c = editorReadKey();
    static int quitTimes = QUIT_TIMES;

    switch (c)
    {
    case '\r':
        editorInsertNewLine();
        break;
    case CTRL_KEY('q'):
        if (document.dirty && quitTimes > 0)
        {
            editorSetStatusMessage("\x1b[1;5m(!)\x1b[m File has unsaved changes. "
                                   "Press Ctrl+Q %d more times to quit.",
                                   quitTimes);
            quitTimes--;
            return;
        }

        clearScreeen();
        exit(0);
        break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY)
            editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;
    case CTRL_KEY('s'):
        editorSave();
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
    case CTRL_KEY('l'):
    case ESC_CHAR:
        break;
    default:
        editorInsertChar(c);
        break;
    }

    quitTimes = QUIT_TIMES;
}

static char *editorPrompt(const char *prompt)
{
    size_t bufferSize = 128;
    char *buffer = malloc(bufferSize);
    buffer[0] = '\0';

    size_t bufferLen = 0;

    while (1)
    {
        editorSetStatusMessage(prompt, buffer);
        editorRefreshScreen();

        const int c = editorReadKey();

        if (c == ESC_CHAR)
        {
            editorSetStatusMessage("");
            free(buffer);

            return NULL;
        }
        else if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        {
            if (bufferLen != 0)
                buffer[--bufferLen] = '\0';
        }
        else if (c == '\r')
        {
            if (bufferLen != 0)
            {
                editorSetStatusMessage("");
                return buffer;
            }
        }
        else if (!iscntrl(c) && c < 128)
        {
            if (bufferLen == bufferSize - 1)
            {
                bufferSize *= 2;
                buffer = realloc(buffer, bufferSize);
            }

            buffer[bufferLen++] = c;
            buffer[bufferLen] = '\0';
        }
    }
}

int main(int argc, char *argv[])
{
    if (enableRawMode(&config.origTermios) != 0)
        die("enableRawMode");

    atexit(resetTerminal);
    initEditor();

    if (argc >= 2)
        editorOpen(argv[1]);

    editorSetStatusMessage("HELP : Ctrl+S = save | Ctrl+Q = quit");

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeyPress();
    }

    return 0;
}
