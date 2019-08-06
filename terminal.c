#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>

#include "terminal.h"

int enableRawMode(struct termios *t)
{
    if (tcgetattr(STDIN_FILENO, t) == -1)
        return -1;

    struct termios raw = *t;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        return -1;

    return 0;
}

int disableRawMode(struct termios *t)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, t) == -1)
        return -1;

    return 0;
}

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

void clearScreeen()
{
    // clear the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    // reposition the cursor to the top left corner
    write(STDOUT_FILENO, "\x1b[H", 3);
}