#ifndef IO_CTRL_H
#define IO_CTRL_H

#include "common.h"

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

int index_len = 0;

#endif