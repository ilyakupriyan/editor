/* *** Includes *** */

#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <stdlib.h>

/* *** Defines *** */

#define EDITOR_VERSION "0.1.0"
#define EDITOR_TAB_SIZE 4
#define EDIOTR_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
	BACKSPACE = 127,
	ARROW_RIGHT = 1000,
	ARROW_LEFT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY
};

/* *** Data *** */

typedef struct editor_row_s {
	int size;
	int render_size;
	char *chars;
	char *render;
} editor_row_t;

struct editorConfig {
	int cx, cy;
	int render_cx;
	int row_offset;
	int col_offset;
	int screen_rows;
	int screen_cols;
	int dirty;
	int num_rows;
	char status_msg[80];
	time_t status_msg_time;
	editor_row_t *row;
	char *file_name;
	struct termios orig_termios;
};

struct editorConfig E;

/* *** Prototypes *** */

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';

				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
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

/* *** Row operations *** */

int editorRowCxToRx(editor_row_t *row, int cx) 
{
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') {
			rx += (EDITOR_TAB_SIZE - 1) - (rx % EDITOR_TAB_SIZE);
		}
		rx++;
	}
	
	return rx;
}

int editorRowRxToCx(editor_row_t *row, int rx) 
{
	int curr_rx = 0;
	int cx;

	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t') {
			curr_rx += (EDITOR_TAB_SIZE - 1) - (curr_rx % EDITOR_TAB_SIZE);
		}
		curr_rx++;

		if (curr_rx > rx) return cx;
	}

	return cx;
}

void editorUpdateRow(editor_row_t *row)
{
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') tabs++;
	}

	free (row->render);
	row->render = malloc(row->size + tabs*(EDITOR_TAB_SIZE - 1) + 1);

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % EDITOR_TAB_SIZE != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}

	row->render[idx] = '\0';
	row->render_size = idx;
}

void editorInsertRow(int at, char *s, size_t len)
{
	if (at < 0 || at > E.num_rows) return;

	E.row = realloc(E.row, sizeof(editor_row_t) * (E.num_rows + 1));
	memmove(&E.row[at + 1], &E.row[at], sizeof(editor_row_t) * (E.num_rows - at));

	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);

	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].render_size = 0;
	E.row[at].render = NULL;

	editorUpdateRow(&E.row[at]);

	E.num_rows++;
	E.dirty++;
}

void editorFreeRow(editor_row_t *row)
{
	free(row->render);
	free(row->chars);
}

void editorDelRow(int at)
{
	if (at < 0 || at >= E.num_rows) return;

	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(editor_row_t) * (E.num_rows - at - 1));
	E.num_rows--;
	E.dirty++;
}

void editorRowInsertChar(editor_row_t *row, int index, int character)
{
	if (index < 0 || index > row->size) index = row->size;
	row->chars = (char *) realloc(row->chars, row->size + 2);

	memmove(&row->chars[index + 1], &row->chars[index], row->size - index + 1);

	row->size++;
	row->chars[index] = character;

	editorUpdateRow(row);

	E.dirty++;
}

void editorRowAppendString(editor_row_t *row, char *s, size_t len)
{
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(editor_row_t *row, int index_char)
{
	if (index_char < 0 || index_char >= row->size) return;

	memmove(&row->chars[index_char], &row->chars[index_char + 1], row->size - index_char);
	row->size--;
	editorUpdateRow(row);

	E.dirty++;
}

/* *** Editor opertations *** */

void editorInsertChar(int c)
{
	if (E.cy == E.num_rows) {
		editorInsertRow(E.num_rows, "", 0);
	}

	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewLine() 
{
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
	} else {
		editor_row_t *row = &E.row[E.cy];

		editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
		row = &E.row[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cy++;
	E.cx = 0;
}

void editorDeleteChar()
{
	if (E.cy == E.num_rows) return;
	if (E.cx == 0 && E.cy == 0) return;

	editor_row_t *row = &E.row[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(row, E.cx - 1);
		E.cx--;
	} else {
		E.cx = E.row[E.cy - 1].size;
		editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy--;
	}
}
/* *** File I/O *** */

char *editorRowsToString(int *buf_len)
{
	int total_len = 0;
	int j;
	for (j = 0; j < E.num_rows; j++) {
		total_len += E.row[j].size + 1;
	}
	*buf_len = total_len;

	char *buf = (char *) malloc(sizeof(char)*total_len);
	char *ptr = buf;
	for (j = 0; j < E.num_rows; j++) {
		memcpy(ptr, E.row[j].chars, E.row[j].size);
		ptr += E.row[j].size;
		*ptr = '\n';
		ptr++;
	}

	return buf;
}

void editorOpen(char *file_name)
{
	free(E.file_name);
	E.file_name = strdup(file_name);

	FILE *fp = fopen(file_name, "r");
	if (!fp) die("fopen");
	
	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		while (line_len > 0 && 
				(line[line_len -1] == '\n' || line[line_len - 1] == '\r'))
					line_len--;

		editorInsertRow(E.num_rows, line, line_len);
	}

	free(line);
	fclose(fp);
	
	E.dirty = 0;
}

void editorSave() 
{
	if (E.file_name == NULL) {
		E.file_name = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (E.file_name == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
	}

	int len;
	char *buf = editorRowsToString(&len);

	int fd = open(E.file_name, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) != -1) {
				close(fd);
				free(buf);

				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/* *** Find *** */

void editorFindCallback(char *query, int key)
{
	static int last_match = -1;
	static int direction = 1;

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = -1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	} else {
		last_match = -1;
		direction = -1;
	}

	if (last_match == -1) 
		direction = 1;
	
	int current = last_match;
	int i;
	for (i = 0; i < E.num_rows; i++) {
		current += direction;
		
		if (current == -1) {
			current = E.num_rows - 1;
		} else if (current == E.num_rows) {
			current = 0;
		}
			

		editor_row_t *row = &E.row[current];
		char *match = strstr(row->render, query);
		if (match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			E.row_offset = E.num_rows;
			break;
		}
	}
}

void editorFind()
{
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_colloff = E.col_offset;
	int saved_rowoff = E.row_offset;

	char *query = editorPrompt("Search: %s (Use Esc/Arrows/Enter)", editorFindCallback);
	if (query) {
		free(query);
	} else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.col_offset = saved_colloff;
		E.row_offset = saved_rowoff;
	}

	
}

/* *** Appending buffer *** */

struct abuf_s {
	char *b;
	int len;
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

void editorScroll()
{
	E.render_cx = 0;

	if (E.cy < E.num_rows) {
		E.render_cx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}

	if (E.cy < E.row_offset) {
		E.row_offset = E.cy;
	}

	if (E.cy >= E.row_offset + E.screen_rows) {
		E.row_offset = E.cy - E.screen_rows + 1;
	}
	if (E.render_cx < E.col_offset) {
		E.col_offset = E.render_cx;
	}
	if (E.render_cx >= E.col_offset + E.screen_cols) {
		E.col_offset = E.render_cx - E.col_offset + 1;
	}
}

void editorDrawRows(struct abuf_s *bf)
{
	int y;

	for (y = 0; y < E.screen_rows; y++) {

		int file_row = y + E.row_offset;

		if (file_row >= E.num_rows) {
			if (E.num_rows == 0 && y == E.screen_rows / 3) {

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
		} else {
			int len = E.row[file_row].render_size - E.col_offset;

			if (len < 0) len = 0;

			if (len > E.screen_cols) len = E.screen_cols;
			abAppend(bf, &E.row[file_row].render[E.col_offset], len);
		}

		abAppend(bf, "\x1b[K", 3);
		abAppend(bf, "\r\n" , 2);
	}
}

void editorDrawStatusBar(struct abuf_s *ab) 
{
	abAppend(ab, "\x1b[7m", 4);

	char status[80];
	char rstatus[80];

	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
						E.file_name ? E.file_name : "[No name]", E.num_rows, 
						E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.num_rows);
	if (len > E.screen_cols) len = E.screen_cols;
	abAppend(ab, status, len);

	while (len < E.screen_cols) {

		if (E.screen_cols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}

	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf_s *ab)
{
	abAppend(ab, "\x1b[K", 3);
	int msg_len = strlen(E.status_msg);

	if (msg_len > E.screen_cols) msg_len = E.screen_cols;

	if (msg_len && time(NULL) - E.status_msg_time < 5) {
		abAppend(ab, E.status_msg, msg_len);
	}
}

void editorRefreshScreen()
{
	editorScroll();

	struct abuf_s ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH",   (E.cy - E.row_offset) + 1, 
												(E.render_cx - E.col_offset) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...)
{
	va_list arg_print;
	va_start(arg_print, fmt);
	vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, arg_print);
	va_end(arg_print);

	E.status_msg_time = time(NULL);
}

/* *** Input *** */

char *editorPrompt(char *prompt, void (* callback)(char *, int))
{
	size_t buf_size = 128;
	char *buf = malloc(buf_size);


	size_t buf_len = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();


		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buf_len != 0) buf[--buf_len] = '\0';
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) 
				callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buf_len != 0) {
				editorSetStatusMessage("");
				if (callback)
					callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buf_len == buf_size - 1) {
				buf_size *= 2;
				buf = realloc(buf, buf_size);
			}
			buf[buf_len++] = c;
			buf[buf_len] = '\0';
		}

		if (callback)
			callback(buf, c);
	}
}

void editorMoveCursor(int key) 
{
	editor_row_t *row = (E.cy >= E.num_rows) ? NULL : & E.row[E.cy];

	switch (key) {
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			} else if (E.cx == 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.num_rows) {
				E.cy++;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size) {
				E.cx++;
			} else if (row && E.cx == row->size) {
				E.cy++;
				E.cx = 0;
			}
			break;
	}

	row = (E.cy >= E.num_rows) ? NULL : &E.row[E.cy];
	int row_len = row ? row->size : 0;
	if (E.cx > row_len) {
		E.cx = row_len;
	}
}

void editorProccessKeypress()
{
	static int  quit_times = EDIOTR_QUIT_TIMES;
	int c = editorReadKey();

	switch(c) {
		case '\r':
			editorInsertNewLine();
			break;

		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editorSetStatusMessage("WARNING!!! File has unsaved changes. ", 
									"Press CTRL + Q %d more times for quit.", quit_times);
				quit_times--;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case CTRL_KEY('s'):
			editorSave();
			break;

		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if (E.cy < E.num_rows) {
				E.cx = E.row[E.cy].size;
			}
			break;
		
		case CTRL_KEY('f'):
			editorFind();
			break;

		case BACKSPACE:
		case DEL_KEY:
		case CTRL_KEY('h'):
				if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
				editorDeleteChar();
			break;

		case PAGE_DOWN:
		case PAGE_UP:
			{
				if (c == PAGE_UP) {
					E.cy = E.row_offset;
				} else if (c == PAGE_DOWN) {
					E.cy = E.row_offset + E.screen_rows - 1;
					if (E.cy > E.num_rows) E.cy = E.num_rows;
				}

				int times = E.screen_rows;
				while (times--) {
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;

		case ARROW_LEFT:
		case ARROW_UP:
		case ARROW_RIGHT:
		case ARROW_DOWN:
			editorMoveCursor(c);
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editorInsertChar(c);
			break;
	}

	quit_times = EDIOTR_QUIT_TIMES;;
}

/* *** Init *** */

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.render_cx = 0;
	E.row_offset = 0;
	E.col_offset = 0;
	E.num_rows = 0;
	E.dirty = 0;
	E.file_name = NULL;
	E.row = NULL;
	E.status_msg[0] = '\0';
	E.status_msg_time = 0;


	if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1) die("getWindowSize");
	E.screen_rows -= 2;
}

int main(int argc, char *argv[]) 
{
	enableRawMode();
	initEditor();

	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("HELP: Ctrl + S = Save | Ctrl + Q = quit | Ctr + F = find");

	while (1) {
		editorRefreshScreen();
		editorProccessKeypress();
	}

	return 0;
}