#ifndef ROW_OPERATIONS_H
#define ROW_OPERATIONS_H

typedef struct editor_row_s {
	int idx;
	int size;
	int render_size;
	char *chars;
	char *render;
	unsigned char *hl;
	int hl_open_comment;
} editor_row_t;

#endif