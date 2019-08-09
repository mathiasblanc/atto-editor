pico: atto.c
	$(CC) atto.c stringbuffer.c terminal.c -o atto -Wall -Wextra -pedantic -std=c99
