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

void cmd_execute(char *cmdbuf, void *d)
{
	struct session *s;
	struct obbysess *os;

	s = session_current();
	os = s ? s->s_obby : NULL;

	switch (cmdbuf[0]) {
		default:
			if (os) {
				char *msg = obby_escape_string(cmdbuf, 0);
				obbysess_enqueue_command(os, "obby_message:%s\n",
						msg);
				free(msg);
			}
			break;

		case '\0':
			break;

		case ':':
			__dbgout("got command: %s\n", &cmdbuf[1]);
			if (!strcmp(&cmdbuf[1], "q")) G.state = NSTATE_LEAVING;
			else if (os && !strncmp(&cmdbuf[1], "s ", 2)) {
				obbysess_enqueue_command(os, "%s\n",
						&cmdbuf[3]);
			} else if (!strncmp(&cmdbuf[1], "nick ", 5)) {
				free(G.nick);
				G.nick = strdup(&cmdbuf[6]);
			} else if (!strncmp(&cmdbuf[1], "color ", 6)) {
				free(G.color);
				G.color = strdup(&cmdbuf[7]);
			} else if (os && !strncmp(&cmdbuf[1], "subscribe ", 10)) {
				obbysess_subscribe(os, &cmdbuf[11]);
			} else if (
					!strncmp(&cmdbuf[1], "connect ", 7) ||
					!strncmp(&cmdbuf[1], "connect ", 8)
				  ) {
				s = session_create(STYPE_OBBY,
						cmdbuf[8] ? &cmdbuf[9] : G.host,
						G.service, OSTYPE_CLIENT);
				if (!s) {
					fprintf(stderr, "Can't create client connection to %s:%s\n",
							G.host, G.service);
				}
			}
			break;

		case '/':
			__dbgout("got pattern: %s\n", &cmdbuf[1]);
			break;
	}

	editor_clearline(cmded);
}

