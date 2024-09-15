#ifndef FILE_IO_H
#define FILE_IO_H

#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "common.h"

//TODO	Changed for adding editor in parameters
/*
 * @brief			Update frame
 * @param editor	Parameters of editor
 * 
 * @return			None
 */
void editorRefreshScreen();

//TODO	Changed for adding editor in parameters
/*
 * @brief			Set the text in status bar
 * @param fmt		First string for printing
 * 
 * @return			None
 */
void editorSetStatusMessage(const char *fmt, ...);

//TODO	Changed for adding editor in parameters
/*
 * @brief			Print a prompt and launch callback	
 * @param prompt	String of prompt
 * @param callback	Function of callback
 * 
 * @return			String
 */
char *editorPrompt(char *prompt, void (*callback)(char *, int));

#endif