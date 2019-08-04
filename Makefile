pico: pico.c
	$(CC) pico.c stringbuffer.c -o pico -Wall -Wextra -pedantic -std=c99