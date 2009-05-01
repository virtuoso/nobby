#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <gnutls/gnutls.h>
#ifdef USE_SLANG
#include <slang.h>
#include <slcurses.h>
#else
#include <curses.h>
#endif
#include "cobby.h"
#include "nobby-ui.h"

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

static WINDOW *mainwnd;
static WINDOW *screen;
static WINDOW *dbgwin;
static WINDOW *listwin;
static WINDOW *cmdwin;

static struct editor *cmded;

static const char my_name[] = "nobby";
static const char my_version[] = "0.1";

int nobby_state = 0;

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

	//wclear(listwin);
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

static void update_display(int a, int b)
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
static void __dbgout(const char *fmt, ...)
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

void cmd_execute(char *cmdbuf, void *d)
{
	struct obbysess *os = d;
	int cmdlen = strlen(cmdbuf);

	switch (cmdbuf[0]) {
		default:
			obbysess_enqueue_command(os, "obby_message:%s\n",
					cmdbuf);
			break;

		case '\0':
			break;

		case ':':
			__dbgout("got command: %s\n", &cmdbuf[1]);
			if (!strcmp(&cmdbuf[1], "q")) nobby_state = NSTATE_LEAVING;
			else if (!strncmp(&cmdbuf[1], "s ", 2)) {
				cmdbuf[cmdlen++] = '\n';
				cmdbuf[cmdlen++] = 0;
				obbysess_enqueue_command(os, &cmdbuf[3]);
			}
			break;

		case '/':
			__dbgout("got pattern: %s\n", &cmdbuf[1]);
			break;
	}

	editor_killline(cmded, 0, 0, -1);
}

int main(int argc, const char *argv[])
{
	struct obbysess *os;
	int ch;
	struct pollfd fds[2];

	if (argc < 3)
		exit(EXIT_FAILURE);

	os = obbysess_create(argv[1], argv[2], OSTYPE_CLIENT);
	if (!os) {
		fprintf(stderr, "Can't create client connection to %s:%s\n",
				argv[1], argv[2]);
		exit(EXIT_FAILURE);
	}

	obby_setdbgfn(__dbgout);
	obby_setmsgfn(__chatout);

	memset(&fds, 0, sizeof(fds));
	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[1].fd = os->os_sock;
	fds[1].events = POLLIN;

	screen_init();

	cmded = editor_create(cmdwin, os);
	if (!cmded)
		exit(EXIT_FAILURE);

	editor_addline(cmded, 0, 0, NULL, 0);

	while (OS_ISOK(os) && nobby_state < NSTATE_LEAVING) {
		obbysess_do(os);

		if (os->os_state == OSSTATE_SHOOKHANDS && nobby_state == 0) {
			obbysess_join(os, "shisha", "ff0000");
			nobby_state = NSTATE_CONNECTED;
		}
		update_display(os->os_state, nobby_state);

		show_lists(os);
		ch = getch();
		editor_gotchar(cmded, ch);

		poll(fds, 2, 10);
	}
	screen_end();

	obbysess_destroy(os);

	return 0;
}

