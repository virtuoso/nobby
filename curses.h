#ifndef __CURSES_H__
#define __CURSES_H__

#ifdef USE_SLANG
#include <slang.h>
#include <slcurses.h>
#else
#include <curses.h>
#endif /* USE_SLANG */

#ifdef USE_SLANG
#define meta(a, b) do {} while (0)
#define vwprintw(__w, __fmt, __args) \
	do { \
		char *__buf; \
		vasprintf(&__buf, __fmt, __args); \
		waddstr(__w, __buf); \
		free(__buf); \
	} while (0);
#endif

#endif /* __CURSES_H__ */

