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
int editor_gotchar(struct editor *e, int ch);

#define MAX_SESSIONS 16

struct session {
	int s_type;
	union {
		struct obbysess *s_obby;
	};
};

enum {
	STYPE_NONE = 0,
	STYPE_OBBY,
};

struct session *session_create(int type, ...);
void session_destroy(struct session *s);
struct session *session_current(void);

extern int nobby_state;
void cmd_execute(char *cmdbuf, void *os);

enum {
	NSTATE_NONE = 0,
	NSTATE_CONNECTED,
	NSTATE_LEAVING,
};

#endif /* __NOBBY_UI_H__ */

