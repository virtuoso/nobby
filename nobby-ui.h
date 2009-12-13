#ifndef __NOBBY_UI_H__
#define __NOBBY_UI_H__

void screen_resize(void);

struct editor {
	WINDOW *e_win;
	char **e_buf;
	/* be careful with the concept of lines:
	 *  - e_lines == amount of lines in the buffer (0 means empty)
	 *  - e_curline == number of current line (0 means first line)
	 */
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
int editor_addchunk(struct editor *e, int line, int pos, char *buf,
		unsigned f);

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
void session_destroy(int sn);
struct session *session_current(void);

extern int nobby_state;
void cmd_execute(char *cmdbuf, void *os);

enum {
	NSTATE_NONE = 0,
	NSTATE_CONNECTED,
	NSTATE_LEAVING,
};

extern struct editor *cmded;
extern struct editor *texted;

struct global_conf {
	char *nick;
	char *color;
	const char *host;
	const char *service;

	int state;
};

extern struct global_conf G;

void __dbgout(const char *fmt, ...);

#endif /* __NOBBY_UI_H__ */

