#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <getopt.h>
#include <gnutls/gnutls.h>
#include "curses.h"
#include "cobby.h"
#include "nobby-ui.h"

static WINDOW *mainwnd;
static WINDOW *screen;
static WINDOW *dbgwin;
static WINDOW *listwin;
static WINDOW *cmdwin;

struct editor *cmded;

static struct session *sessions[MAX_SESSIONS];
static int nsessions, cursession;

static const char my_name[] = "nobby";
static const char my_version[] = "0.1";

struct global_conf G;

struct layout {
	/* debug window: top or bottom */
	int debug_y;
	int debug_h;
	int debug_w;
	int debug_x;

	/* user list and doc list: left or right */
	int lists_y;
	int lists_h;
	int lists_x;
	int lists_w;

	/* chat */
	int chat_y;
	int chat_h;
	int chat_w;
	int chat_x;

	/* command */
	int cmd_x;
	int cmd_y;
	int cmd_w;
	int cmd_h;
	int w, h;
};

static struct layout layout;

void layout_redo(void)
{
	int w = COLS, h = LINES;

	layout.cmd_x = 0;
	layout.cmd_w = w;
	layout.cmd_y = --h;
	layout.cmd_h = 1;

	layout.lists_y = 0;
	layout.lists_h = h;
	w -= layout.lists_w = 20;
	layout.lists_x = w;

	h -= layout.debug_h = LINES / 4;
	layout.debug_y = h;
	layout.debug_x = 0;
	layout.debug_w = w;

	layout.chat_y = 0;
	layout.chat_h = h;
	layout.chat_w = w;
	layout.chat_x = 0;

	layout.w = COLS;
	layout.h = LINES;
}

void banner(void)
{
	wprintw(screen, "%s v%s\nEnjoy!\n"
			"Press F10 or type :q to quit.\n",
			my_name, my_version);
}

void show_lists(struct obbysess *os)
{
	int i, y = 1;

	werase(listwin);
	mvwaddstr(listwin, y++, 1, "users:");

	for (i = 0; i < os->os_eusers; i++) {
		mvwprintw(listwin, y++, 1, "[%s:%d]",
				os->os_users[i]->ou_name,
				os->os_users[i]->ou_obbyuid);
	}

	mvwaddstr(listwin, y++, 1, "documents:");
	for (i = 0; i < os->os_edocs; i++) {
		mvwprintw(listwin, y++, 1, "[%s:%d]",
				os->os_docs[i]->od_name,
				os->os_docs[i]->od_nusers);
	}
}

void screen_init(void) {
	mainwnd = initscr();
	noecho();
	raw();
	keypad(stdscr, TRUE);
	nodelay(mainwnd, TRUE);
	meta(mainwnd, TRUE);
	clearok(mainwnd, FALSE);
	refresh();
	wrefresh(mainwnd);

	layout_redo();
	screen = newwin(layout.chat_h, layout.chat_w, layout.chat_y,
			layout.chat_x);
	listwin = newwin(layout.lists_h, layout.lists_w, layout.lists_y,
			layout.lists_x);
	dbgwin = newwin(layout.debug_h, layout.debug_w, layout.debug_y,
			layout.debug_x);
	cmdwin = newwin(layout.cmd_h, layout.cmd_w, layout.cmd_y,
			layout.cmd_x);
	leaveok(mainwnd, TRUE);
	/*box(screen, ACS_VLINE, ACS_HLINE);
	box(listwin, ACS_VLINE, ACS_HLINE);
	box(dbgwin, ACS_VLINE, ACS_HLINE);*/
	scrollok(dbgwin, TRUE);
	scrollok(cmdwin, TRUE);
	scrollok(screen, TRUE);
	banner();
}

static void update_display(void)
{
	curs_set(0);
	wrefresh(screen);
	wrefresh(listwin);
	wrefresh(dbgwin);
	wrefresh(cmdwin);
	refresh();
}

void screen_end(void) {
	endwin();
}

/* for cobby to output it's diag() to our debug window */
void __dbgout(const char *fmt, ...)
{
	va_list args;
	FILE *f;

	f = fopen("/tmp/nobby", "a");
	va_start(args, fmt);
	vwprintw(dbgwin, fmt, args);
	if (f)
		vfprintf(f, fmt, args);
	fclose(f);
	va_end(args);
}

static void __chatout(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vwprintw(screen, fmt, args);
	va_end(args);
}

static int __obby_notify_callback(void *priv, struct obbyevent *oe)
{
	int sn = (int)priv;
	struct obbysess *os = sessions[sn]->s_obby;

	switch (oe->oe_type) {
		case OETYPE_USER_JOINED:
		case OETYPE_USER_PARTED:
			__chatout("--- %s has %sed\n", oe->oe_username,
					oe->oe_type == OETYPE_USER_JOINED
					? "join" : "part");
		case OETYPE_USER_KNOWN:
		case OETYPE_DOC_KNOWN:
			show_lists(os);
			break;

		case OETYPE_DOC_OPEN:
			__chatout("+++ opening %s\n", oe->oe_docname);
			break;

		case OETYPE_DOC_GETCHUNK:
			__chatout("+++ HERE GOES:\n%s\n", oe->oe_message);
			break;

		case OETYPE_CHAT_MESSAGE:
			__chatout("<%s> %s\n", oe->oe_username,
					obby_unescape_string(oe->oe_message,
						-1));
			break;

		case OETYPE_DEBUG_MESSAGE:
			__dbgout(oe->oe_message);
			break;

		default:
			break;
	}

	return 0;
}

struct session *session_create(int type, ...)
{
	struct session *s;
	va_list args;
	char *host, *service;
	int conntype;

	s = malloc(sizeof(struct session));
	if (!s)
		return NULL;

	va_start(args, type);
	switch (type) {
		case STYPE_OBBY:
			/* host, service, connection type */
			host = va_arg(args, char *);
			service = va_arg(args, char *);
			conntype = va_arg(args, int);
			s->s_obby = obbysess_create(host, service, conntype);
			if (s->s_obby) {
				obbysess_set_notify_callback(s->s_obby,
						__obby_notify_callback,
						(void *)nsessions);
				break;
			}
			/* otherwise fall through */

		default:
			free(s);
			return NULL;
	}
	va_end(args);

	s->s_type = type;

	sessions[nsessions++] = s;
	if (nsessions == 1)
		cursession = 0;

	return s;
}

void session_destroy(struct session *s)
{
	switch (s->s_type) {
		case STYPE_OBBY:
			obbysess_destroy(s->s_obby);
			break;

		default:
			break;
	}
}

struct session *session_current(void)
{
	return sessions[cursession];
}

int session_get_fd(int sn)
{
	switch (sessions[sn]->s_type)
	{
		case STYPE_OBBY:
			return sessions[sn]->s_obby->os_sock;

		default:
			break;
	}

	return -1;
}

int session_do(struct session *s)
{
	switch (s->s_type) {
		case STYPE_OBBY:
			obbysess_do(s->s_obby);
			if (s->s_obby->os_state == OSSTATE_SHOOKHANDS) {
				obbysess_join(s->s_obby, G.nick, G.color);
				G.state = NSTATE_CONNECTED;
			}
			break;

		default:
			return -1;
	}

	return 0;
}

void sessions_do(void)
{
	int n;

	for (n = 0; n < nsessions; n++)
		(void)session_do(sessions[n]);
}

static const struct option options[] = {
	{ "nick",               1, 0, 'n' },
	{ "color",              1, 0, 'c' },
	{ "help",               0, 0, 'h' },
	{ NULL,                 0, 0, 0   },
};

static const char *options_desc[] = {
	"specify your desired nickname",
	"specify your desired color",
	"print help message and exit",
};

static const char *optstr = "n:c:h";

static void usage(const char *msg, int exit_code)
{
	int i;

	if (msg)
		fprintf(stderr, "Error: %s\n", msg);

	fprintf(stderr, "Usage: %s [OPTIONS] host service\n"
			"OPTIONS:\n", my_name);
	for (i = 0; options[i].name; i++)
		fprintf(stderr, "\t-%c, --%s\t%s\n",
				options[i].val,
				options[i].name,
				options_desc[i]);

	exit(exit_code);
}

int main(int argc, char **argv)
{
	struct session *s;
	int ch, loptidx, c, n = 0;
	struct pollfd fds[MAX_SESSIONS];

	for (;;) {
		c = getopt_long(argc, argv, optstr, options, &loptidx);
		if (c == -1)
			break;

		switch (c) {
			case 'n':
				G.nick = strdup(optarg);
				break;

			case 'c':
				G.color = strdup(optarg);
				break;

			case 'h':
				usage(NULL, EXIT_SUCCESS);

			default:
				usage("invalid arguments", EXIT_FAILURE);
		}
	}

	if (argc == optind)
		usage("too few arguments", EXIT_FAILURE);

	G.host = argv[optind++];
	G.service = argv[optind++];
	if (!G.host || !G.service)
		usage("too few arguments", EXIT_FAILURE);

	if (!G.nick)
		G.nick = getenv("USER");
	if (!G.nick)
		G.nick = getenv("LOGNAME");

	if (!G.nick)
		usage("can't determine your nickname", EXIT_FAILURE);

	if (!G.color)
		G.color = strdup("ffffff");

	memset(&fds, 0, sizeof(fds));

	screen_init();

	cmded = editor_create(cmdwin, NULL);
	if (!cmded)
		exit(EXIT_FAILURE);

	editor_addline(cmded, 0, 0, NULL, 0);

	while (G.state < NSTATE_LEAVING) {
		sessions_do();

		update_display();

		ch = getch();
		editor_gotchar(cmded, ch);

		if (!n) {
			for (c = 0; c < nsessions; c++) {
				fds[c].fd = session_get_fd(c);
				fds[c].events = POLLIN;
			}
			fds[c].fd = 0;
			fds[c].events = POLLIN;
			n = poll(fds, c + 1, 100);
		} else
			n--;
	}
	screen_end();

	session_destroy(s);

	return 0;
}

