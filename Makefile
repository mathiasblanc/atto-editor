pico: pico.c
	$(CC) pico.c stringbuffer.c terminal.c -o pico -Wall -Wextra -pedantic -std=c99