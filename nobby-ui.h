#ifndef __NOBBY_UI_H__
#define __NOBBY_UI_H__

struct editor {
	WINDOW *e_win;
	char **e_buf;
	unsigned e_lines;
	unsigned e_curline;
	unsigned e_curpos;
	void *e_priv;
};

struct editor *editor_create(WINDOW *win, void *priv);
void editor_destroy(struct editor *e);
int editor_addline(struct editor *e, int line, int pos, char *buf, unsigned f);
int editor_killline(struct editor *e, int line, int pos, ssize_t len);

extern int nobby_state;
void cmd_execute(char *cmdbuf, void *os);

enum {
	NSTATE_NONE = 0,
	NSTATE_CONNECTED,
	NSTATE_LEAVING,
};

#endif /* __NOBBY_UI_H__ */

