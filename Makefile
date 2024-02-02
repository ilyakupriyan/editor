all: editor.c
	$(CC) editor.c -o editor -Wall -Wextra -pedantic -std=c99