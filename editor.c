/* *** Includes *** */

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

/* *** Defines *** */
#define EDITOR_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_RIGHT = 1000,
    ARROW_LEFT,
    ARROW_UP,
    ARROW_DOWN
};

/* *** Data *** */

struct editorConfig {
    int cx, cy;
    int screen_rows;
    int screen_cols;
    struct termios orig_termios;
};

struct editorConfig E;

/* *** Terminal *** */
void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT| ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=  (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }

        return '\x1b';
    } else {
        return c;
    }

    return c;
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    
    while (i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d:%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;

        return 0;
    }
}

/* *** Appending buffer *** */

struct abuf_s {
    char *b;
    int len
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf_s *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
        
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf_s *ab)
{
    free(ab->b);
}

/* *** Output *** */

void editorDrawRows(struct abuf_s *bf)
{
    int y;

    for (y = 0; y < E.screen_rows; y++) {
        if (y == E.screen_rows / 3) {
            char welcome[80];
            int welcome_len = snprintf(welcome, sizeof(welcome), "Kupriyan-editor -- version %s", EDITOR_VERSION);
            if (welcome_len > E.screen_cols) welcome_len = E.screen_cols;
            
            int padding = (E.screen_cols - welcome_len) / 2;
            if (padding) {
                abAppend(bf, "~", 1);
                padding--;
            }
            while (padding--) abAppend(bf, " ", 1);
            abAppend(bf, welcome, welcome_len);
        } else {
            abAppend(bf, "~", 1);
        }

        abAppend(bf, "\x1b[K", 3);
        if (y < E.screen_rows - 1)
            abAppend(bf, "\r\n" , 2);
    }
}

void editorRefreshScreen()
{
    struct abuf_s ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/* *** Input *** */

void editorMoveCursor(int key) 
{
    switch (key) {
        case ARROW_UP:
            E.cy--;
            break;
        case ARROW_LEFT:
            E.cx--;
            break;
        case ARROW_DOWN:
            E.cy++;
            break;
        case ARROW_RIGHT:
            E.cx++;
            break;
    }
}

void editorProccessKeypress()
{
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case ARROW_LEFT:
        case ARROW_UP:
        case ARROW_RIGHT:
        case ARROW_DOWN:
            editorMoveCursor(c);
            break;
    }
}

/* *** Init *** */

void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1) die("getWindowSize");
}

int main(void) 
{
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProccessKeypress();
    }

    return 0;
}