#ifndef EDITOR_OPERATIONS_H
#define EDITOR_OPERATIONS_H

#include "common.h"
#include "row_operations.h"
#include <time.h>
#include <termios.h>
#include <unistd.h>

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
	struct editorSyntax *syntax;
};

#endif