#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curses.h>
#include "nobby-ui.h"

struct editor *editor_create(WINDOW *win, void *priv)
{
	struct editor *e;

	e = malloc(sizeof(struct editor));
	if (!e)
		return NULL;

	e->e_win = win;
	e->e_buf = NULL;
	e->e_lines = 0;
	e->e_curline = 0;
	e->e_curpos = 0;
	e->e_priv = priv;

	return e;
}

void editor_destroy(struct editor *e)
{
	if (e->e_buf)
		free(e->e_buf);

	free(e);
}

#define MAXLINES 16384
int editor_realloclines(struct editor *e, int delta)
{
	int lines = e->e_lines + delta;
	int i;

	if (lines < 0 || lines > MAXLINES)
		return -1;

	if (e->e_buf && lines == e->e_lines)
		return 0;

	if (lines < e->e_lines)
		for (i = lines; i < e->e_lines; i++)
			if (e->e_buf[i])
				free(e->e_buf[i]);

	e->e_buf = realloc(e->e_buf, lines);
	if (!e->e_buf)
		return -1;

	if (lines > e->e_lines)
		for (i = e->e_lines; i < lines; i++)
			e->e_buf[i] = NULL;

	return lines;
}

int editor_addline(struct editor *e, int line, int pos, char *buf, unsigned f)
{
	int minlen = pos + (buf ? strlen(buf) : 0) + 1;

	/* check if the line exists */
	if (!e->e_buf || line > e->e_lines)
		if (editor_realloclines(e, line - e->e_lines) < 0)
			return -1;

	if (!e->e_buf[line]) {
		e->e_buf[line] = realloc(e->e_buf[line], minlen);
		if (!e->e_buf[line])
			return -1;

		memset(e->e_buf[line], 0, minlen);
	} else {
		int oldsz = strlen(e->e_buf[line]);

		e->e_buf[line] = realloc(e->e_buf[line],
				pos > oldsz ? minlen
				: oldsz + (buf ? strlen(buf) : 0) + 1);
		if (!e->e_buf[line])
			return -1;
	}

	if (buf)
		memcpy(e->e_buf[line] + pos, buf, strlen(buf) + 1);

	return 0;
}

int editor_killline(struct editor *e, int line, int pos, ssize_t len)
{
	int sz;

	if (line > e->e_lines)
		return -1;

	if (!e->e_buf[line])
		return 0;

	if (line == e->e_lines) {
		editor_realloclines(e, -1);
		return 0;
	}

	if (len == -1) {
		free(e->e_buf[line]);
		e->e_buf[line] = NULL;
		return 0;
	}

	sz = strlen(e->e_buf[line]) - pos - len + 1;
	memmove(e->e_buf[line] + pos, e->e_buf[line] + pos + len, sz);
	e->e_buf[line][pos + sz] = 0;
	e->e_buf[line] = realloc(e->e_buf[line], pos + sz);

	return 0;
}

void editor_backspace(struct editor *e)
{
	if (!e->e_buf[e->e_curline] || !e->e_curpos)
		return;

	e->e_buf[e->e_curline][--e->e_curpos] = 0;

	werase(e->e_win);
	/* XXX: e: first displayed line */
	waddstr(e->e_win, e->e_buf[e->e_curline]);
}

void editor_clearline(struct editor *e)
{
	if (!e->e_buf[e->e_curline] || !e->e_curpos)
		return;

	e->e_curpos = 0;
	memset(e->e_buf[e->e_curline], 0, strlen(e->e_buf[e->e_curline]));

	werase(e->e_win); /* XXX: if needed */
}

void editor_killword(struct editor *e)
{
	char *s, *line = e->e_buf[e->e_curline];
	int pos = e->e_curpos;

	if (!line || !pos)
		return;

	/* cut all trailing whitespace first */
	for (;
		pos > 0
		&& isspace(line[pos - 1]);
		line[--pos] = 0
	    );

	/* then, cut the last word */
	s = strrchr(line, ' ');
	if (s) {
		pos = s - line + 1;
		line[pos] = 0;
		werase(e->e_win);
		waddstr(e->e_win, line);
	} else
		editor_clearline(e);

	e->e_curpos = pos;
}

int editor_gotchar(struct editor *e, int ch)
{
	char s[] = { ch, 0 };

	switch (ch) {
		case ERR:
			break;

		case KEY_F(10):
			G.state = NSTATE_LEAVING;
			break;

		case '\n':
		case '\r':
		case KEY_ENTER:
			waddch(e->e_win, ch);
			cmd_execute(e->e_buf[e->e_curline], e->e_priv);
			editor_clearline(e);
			break;

		case KEY_BACKSPACE:
			editor_backspace(e);
			break;

		case 0x17: /* ^W */
			editor_killword(e);
			break;

		case 0x15: /* ^U */
			editor_clearline(e);
			break;

		default:
			waddch(e->e_win, ch);
			wrefresh(e->e_win);
			editor_addline(e, e->e_curline, e->e_curpos++, s, 0);
			break;
	}

	return 0;
}

