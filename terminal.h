#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

/* 
* Put terminal into raw mode : 
* disable canonical mode -> input is read byte by byte instead of line by line
* disable ctrl+* keys
* disable OPOST (output processing) -> now requires explicit \r before \n
*/
int enableRawMode(struct termios *t);

/* 
* Reset terminal settings to their original values
*/
int disableRawMode(struct termios *t);

/*
* Use ioctl with TIOCGWINSZ flag to retrieve the terminals number of rows and cols.
* We use a fallback in case ioctl fails on some systems by moving the cursor to the bottom
* right corner of the screen and retrieve the cursor position.
*/
int getWindowSize(int *rows, int *cols);

/*
* The 'n' command requests and reports the general status of the VT100. 
* Parameter 6 reports the active position of the cursor.
* The x and y coordinates are read from stdin in the form 27[30;211R
 */
int getCursorPosition(int *rows, int *cols);

/*
* We use VT100 escape char 0x1b (27) followed by a [ and one or two more bytes 
* depending on the sequence to clear part of the screen and move the cursor.

* @See VT100 command set https://vt100.net/docs/vt100-ug/chapter3.html#ED
 */
void clearScreeen();

#endif
