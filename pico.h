#ifndef PICO_H
#define PICO_H

#include "stringbuffer.h"

#define PICO_VERSION "0.0.1"
#define EDITOR_ROW_DECORATOR "~"
#define EDITOR_ROW_DECORATOR_LEN 1

void die(const char *message);
void disableRawMode();
void enableRawMode();
void clearScreeen();
int getWindowSize(int *rows, int *cols);
int getCursorPosition(int *rows, int *cols);
void initEditor();
char editorReadKey();
void editorRefreshScreen();
void editorProcessKeyPress();
void editorDrawRows(StringBuffer *sb);

#endif