#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <gnutls/gnutls.h>
#include <stdarg.h>
#include "cobby.h"

#if 1
#define diag(__os, __s, __a...) \
	do { \
		dbgfn(__os, "%s(): " __s, __FUNCTION__, ## __a); \
	} while (0);
#else
#define diag(__os, __s, __a...) do {} while (0)
#endif

#define err(__os, __s, __a...) diag(__os, __s, ## __a)

static int __obbysess_create_client(const char *host, const char *port);
static int parse_command(struct obbysess *os, char *cmd);
static void parse_inbuf(struct obbysess *os);
static void send_outbuf(struct obbysess *os);
static struct obbyuser *obbyuser_create(struct obbysess *os, char *name,
		char *net6uid, long color);
static struct obbyuser *obbyuser_find(struct obbysess *os, unsigned uid);
static struct obbyuser *obbyuser_find_by_name(struct obbysess *os, char *name);
static struct obbyuser *obbyuser_find_by_nid(struct obbysess *os, char *nid);
static void obbyuser_free(struct obbyuser *ou);
static void obbysess_free_docs(struct obbysess *os);
static void obbysess_free_users(struct obbysess *os);
static struct obbydoc *obbydoc_create(struct obbysess *os, char *name,
		unsigned obbyuid, unsigned obbyuididx, unsigned nusers);
static void obbydoc_free(struct obbydoc *od);

static void __dbgout(struct obbysess *os, const char *fmt, ...)
{
	va_list args;
	char *msg;

	va_start(args, fmt);
	if (!os || !os->os_notify_user || vasprintf(&msg, fmt, args) == -1)
		fprintf(stderr, fmt, args);
	else {
		obbysess_notify(os, OETYPE_DEBUG_MESSAGE,
				.oe_message = msg);
		free(msg);
	}
	va_end(args);
}

static void (*dbgfn)(struct obbysess *, const char *, ...) = __dbgout;

struct obby_command {
	const char *oc_string;
	int (*oc_handler)(struct obbysess *, char *);
};

/*
 * Protocol "commands": plain text messages with comma-separated
 * operands that optionally (depending on the command) follow.
 * There are generally 2 types of commands, distinguished by their
 * prefixes:
 *  - net6 -- those implemented in libnet6, which is some kind of
 *            a networking layer wrapper for dummies writing in C++:
 *            these generally deal with connecting, encrypting,
 *            state of the connection etc.
 *  - obby -- those implemented in libobby: these provide the
 *            actual obbiness.
 */
/*
 * -- proto command --
 * obby_welcome is issued right after the socket is successfully
 * opened;
 * sender: server
 * args:
 *  + protocol version;
 * no response expected
 */
static int obby_welcome_handler(struct obbysess *os, char *args)
{
	int v;

	v = atoi(args);
	diag(os, "protocol version %d\n", v);

	os->os_proto = v;

	return 0;
}

/*
 * -- proto command --
 * net6_encryption is issued right after 'obby_welcome' to ask the
 * peer to starttls
 * sender: [theoretically] both
 * args:
 *  + 0 if requested by a server, 1 if requested by client;
 * response:
 *  + net6_encryption_ok if tls is supported,
 *    otherwise net6_encryption_fail
 * (I'm fairly sure that the most recent server will even accept
 * the latter, though)
 */
static int net6_encryption_handler(struct obbysess *os, char *args)
{
	int p;

	p = atoi(args);

	if (os->os_type == OSTYPE_CLIENT && p == 0) {
		diag(os, "server requests encryption\n");

		obbysess_enqueue_command(os, "net6_encryption_ok\n");
	} else if (os->os_type == OSTYPE_SERVER && p == 1) {
		diag(os, "client requests encryption\n");
	} else {
		diag(os, "invalid encryption request\n");
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	return 0;
}

static ssize_t __send(struct obbysess *os, const void *data, size_t size)
{
	return os->os_flags & OSFLAG_ENCRYPTED
			? gnutls_record_send(os->os_tlssess, data, size)
			: write(os->os_sock, data, size);
}

static ssize_t __recv(struct obbysess *os, void *data, size_t size)
{
	return os->os_flags & OSFLAG_ENCRYPTED
			? gnutls_record_recv(os->os_tlssess, data, size)
			: read(os->os_sock, data, size);
}

/*
 * -- proto command --
 * net6_encryption_begin is issued in response to net6_encryption_ok,
 * to indicate that starttls should follow immediately
 * sender: [theoretically] both
 * args: none
 * response:
 *  + TLS handshake
 */
static int net6_encryption_begin_handler(struct obbysess *os, char *args)
{
	int n;
	const int kx_prio[] = { GNUTLS_KX_ANON_DH, 0 };

	if (os->os_type == OSTYPE_CLIENT) {
		diag(os, "starting client tls session\n");
		gnutls_global_init();

		gnutls_anon_allocate_client_credentials(&os->os_anoncred);

		gnutls_init(&os->os_tlssess, GNUTLS_CLIENT);
		gnutls_set_default_priority(os->os_tlssess);
		gnutls_kx_set_priority(os->os_tlssess, kx_prio);
		gnutls_credentials_set(os->os_tlssess, GNUTLS_CRD_ANON,
				os->os_anoncred);
		/*gnutls_session_set_ptr(os->os_tlssess, os);*/
		gnutls_transport_set_ptr(os->os_tlssess,
				(gnutls_transport_ptr)os->os_sock);

		do {
			n = gnutls_handshake(os->os_tlssess);
		} while (n == GNUTLS_E_INTERRUPTED || n == GNUTLS_E_AGAIN);

		if (n < 0) {
			err(os, "TLS handshake failed: %d\n", n);
			gnutls_perror(n);
			perror("gnutls");
			os->os_state = OSSTATE_ERROR;
			return -1;
		}

		diag(os, "TLS handshake succeeded\n");
		os->os_flags |= OSFLAG_ENCRYPTED;
		os->os_state = OSSTATE_SHOOKHANDS;
	} else {
		diag(os, "server encryption -- not implemented\n");
		return -1;
	}

	return 0;
}

/*
 * -- proto command --
 * net6_ping is issued after the (I believe) 60 seconds of slience
 * from the other end
 * sender: [theoretically] both
 * args: none
 * response:
 *  + net6_pong
 */
static int net6_ping_handler(struct obbysess *os, char *args)
{
	obbysess_enqueue_command(os, "net6_pong\n");
	return 0;
}

/*
 * -- proto command --
 * obby_sync_init is issued after the user has logged (joined)
 * to communicate all known users and documents to him
 * sender: server
 * args:
 *  + [theoretically] number of items (users and documents) that will
 * follow, practice sometimes disagrees (will have to check obby sources)
 * no response expected
 */
static int obby_sync_init_handler(struct obbysess *os, char *args)
{
	int nitems;

	os->os_state = OSSTATE_JOINED;

	nitems = atoi(args);
	diag(os, "expecting %d users\n", nitems);

	os->os_nitems = nitems;
	memset(&os->os_users, 0, sizeof(os->os_users));

	return 0;
}

static struct obbyuser *obbyuser_create(struct obbysess *os, char *name,
		char *net6uid, long color)
{
	struct obbyuser *ou;

	ou = malloc(sizeof(struct obbyuser));
	if (!ou) {
		os->os_state = OSSTATE_ERROR;
		return NULL;
	}

	ou->ou_name = name;
	ou->ou_net6uid = net6uid;
	ou->ou_color = color;
	ou->ou_obbyuid = -1;
	ou->ou_enctyped = 0;

	return ou;
}

static struct obbyuser *obbyuser_find(struct obbysess *os, unsigned uid)
{
	int i;

	for (i = 0; i < os->os_eusers; i++)
		if (os->os_users[i]->ou_obbyuid == uid)
			return os->os_users[i];

	return 0;
}

static struct obbyuser *obbyuser_find_by_name(struct obbysess *os, char *name)
{
	int i;

	for (i = 0; i < os->os_eusers; i++)
		if (!strcmp(os->os_users[i]->ou_name, name))
			return os->os_users[i];

	return 0;
}

static struct obbyuser *obbyuser_find_by_nid(struct obbysess *os, char *nid)
{
	int i;

	for (i = 0; i < os->os_eusers; i++)
		if (!strcmp(os->os_users[i]->ou_net6uid, nid))
			return os->os_users[i];

	return 0;
}

static void obbyuser_free(struct obbyuser *ou)
{
	free(ou->ou_name);
	free(ou->ou_net6uid);
	free(ou);
}

static void obbysess_free_users(struct obbysess *os)
{
	int i;

	for (i = 0; i < os->os_eusers; i++) {
		obbyuser_free(os->os_users[i]);
		os->os_users[i] = NULL;
	}

	os->os_eusers = 0;
}

static struct obbydoc *obbydoc_create(struct obbysess *os, char *name,
		unsigned obbyuid, unsigned obbyuididx, unsigned nusers)
{
	struct obbydoc *od;

	od = malloc(sizeof(struct obbydoc));
	if (!od) {
		os->os_state = OSSTATE_ERROR;
		return NULL;
	}

	od->od_name = name;
	od->od_obbyuid = obbyuid;
	od->od_obbyuididx = obbyuididx;
	od->od_nusers = nusers;
	od->od_encoding = NULL;

	return od;
}

static void obbydoc_free(struct obbydoc *od)
{
	free(od->od_name);
	if (od->od_encoding)
		free(od->od_encoding);
	free(od);
}

static void obbysess_free_docs(struct obbysess *os)
{
	int i;

	for (i = 0; i < os->os_edocs; i++) {
		obbydoc_free(os->os_docs[i]);
		os->os_docs[i] = NULL;
	}

	os->os_edocs = 0;
}

/*
 * -- proto command --
 * net6_client_join comes either in the 'sync' exchange or when a user
 * joins
 * sender: server
 * args:
 *  + net6 user id;
 *  + username (nick);
 *  + encryption (1/0);
 *  + obby user id;
 *  + color;
 * no response expected
 * (I strongly suspect that earlier versions of the protocol had a
 * different set of arguments)
 */
static int net6_client_join_handler(struct obbysess *os, char *args)
{
	struct obbyuser *ou;
	char *color, *name, *net6uid;
	int n, enc, oid, c;

	/* XXX: older versions of protocol will pass fewer fields */
	n = sscanf(args, "%a[^:]:%a[^:]:%d:%d:%as\n",
			&net6uid,
			&name,
			&enc,
			&oid,
			&color);
	if (n != 5) {
		err(os, "malformed join command: %d, %s\n", n, args);
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	c = strtoul(color, NULL, 16);
	free(color);

	ou = obbyuser_find_by_name(os, name);
	if (ou) {
		if (ou->ou_net6uid)
			free(ou->ou_net6uid);
		ou->ou_net6uid = net6uid;

		free(name);
	} else {
		ou = obbyuser_create(os, name, net6uid, c);
		if (!ou) {
			os->os_state = OSSTATE_ERROR;
			return -1;
		}

		os->os_users[os->os_eusers++] = ou;
	}

	ou->ou_enctyped = enc;
	ou->ou_obbyuid = oid;

	obbysess_notify(os, OETYPE_USER_JOINED, .oe_username = ou->ou_name);

	return 0;
}

/*
 * -- proto command --
 * net6_client_part is to indicate that a user has parted
 * sender: server
 * args:
 *  + net6 user id;
 * no response expected
 */
static int net6_client_part_handler(struct obbysess *os, char *args)
{
	struct obbyuser *ou;

	ou = obbyuser_find_by_nid(os, args);
	if (!ou) {
		err(os, "user %s never existed\n", args);
		return -1;
	}

	ou->ou_obbyuid = -1;
	ou->ou_enctyped = 0;

	obbysess_notify(os, OETYPE_USER_PARTED, .oe_username = ou->ou_name);

	return 0;
}

/*
 * -- proto command --
 * obby_sync_usertable_user comes during the 'sync' exchange to indicate
 * that a user with certain name/color/uids/whatnot is known to a server
 * sender: server
 * args:
 *  + net6 user id;
 *  + username (nick);
 *  + color;
 * no response expected
 * (I strongly suspect that earlier versions of the protocol had a
 * different set of arguments)
 */
static int obby_sync_usertable_user_handler(struct obbysess *os, char *args)
{
	struct obbyuser *ou;
	char *color, *name, *net6uid;
	int n, c;

	n = sscanf(args, "%a[^:]:%a[^:]:%as\n",
			&net6uid,
			&name,
			&color);
	if (n != 3) {
		err(os, "malformed sync command: %d, %s\n", n, args);
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	c = strtoul(color, NULL, 16);
	free(color);

	ou = obbyuser_create(os, name, net6uid, c);
	if (!ou) {
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	os->os_users[os->os_eusers++] = ou;

	obbysess_notify(os, OETYPE_USER_KNOWN, .oe_username = ou->ou_name);

	return 0;
}

/*
 * -- proto command --
 * obby_sync_doclist_document comes during the 'sync' exchange to indicate that
 * a document with certain name/encoding/whatnot exists on a server
 * sender: server
 * args:
 *  + obby user id of the creator;
 *  + creator's document index number (as it was created);
 *  + document name;
 *  + number of users who have this document open;
 *  + encoding of the document;
 * no response expected
 * (I strongly suspect that earlier versions of the protocol had a
 * different set of arguments)
 */
static int obby_sync_doclist_document_handler(struct obbysess *os, char *args)
{
	struct obbydoc *od;
	unsigned obbyuid, obbyuididx, nusers;
	char *name, *enc;
	int n;

	/* XXX: older versions of protocol will pass fewer fields */
	n = sscanf(args, "%d:%d:%a[^:]:%d:%as\n",
			&obbyuid,
			&obbyuididx,
			&name,
			&nusers,
			&enc);
	if (n != 5) {
		err(os, "malformed sync command: %d, %s\n", n, args);
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	od = obbydoc_create(os, name, obbyuid, obbyuididx, nusers);
	if (!od) {
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	od->od_encoding = enc;
	os->os_docs[os->os_edocs++] = od;

	obbysess_notify(os, OETYPE_DOC_KNOWN, .oe_docname = od->od_name);

	return 0;
}

/*
 * -- proto command --
 * obby_sync_final comes during the 'sync' exchange to conclude it
 * sender: server
 * args: none
 * no response expected
 */
static int obby_sync_final_handler(struct obbysess *os, char *args)
{
	if (os->os_nitems != os->os_eusers + os->os_edocs) {
		err(os, "invalid number of items given: %d, "
				"received: %d\n", os->os_nitems,
				os->os_eusers + os->os_edocs);
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	os->os_state = OSSTATE_SYNCED;
	os->os_nitems = 0;

	return 0;
}

/*
 * -- proto command --
 * obby_message is to send a message to the common chatroom
 * sender: both
 * args:
 *  + [only when sent by a server] obby user id of the sender
 *  + message text
 * no response expected
 */
static int obby_message_handler(struct obbysess *os, char *args)
{
	char *p = args;
	struct obbyuser *ou;
	int uid;

	if (!*p) {
		err(os, "malformed message\n");
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	uid = atoi(p);
	ou = obbyuser_find(os, uid);

	p = strchr(p, ':');
	if (!p || !ou) {
		err(os, "malformed message\n");
		os->os_state = OSSTATE_ERROR;
		return -1;
	}

	p++;

	obbysess_notify(os, OETYPE_CHAT_MESSAGE,
			.oe_username = ou->ou_name,
			.oe_message = p
			);

	return 0;
}

#define OBBY_CMD(__s) \
	{ .oc_string = # __s, .oc_handler = __s ## _handler }

static struct obby_command cmdlist[] = {
	OBBY_CMD(obby_welcome),
	OBBY_CMD(net6_encryption),
	OBBY_CMD(net6_encryption_begin),
	OBBY_CMD(net6_ping),
	OBBY_CMD(obby_sync_init),
	OBBY_CMD(net6_client_join),
	OBBY_CMD(net6_client_part),
	OBBY_CMD(obby_sync_usertable_user),
	OBBY_CMD(obby_sync_doclist_document),
	OBBY_CMD(obby_sync_final),
	OBBY_CMD(obby_message),
};

static int __obbysess_create_client(const char *host, const char *port)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int s, sfd;
	long flags;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	s = getaddrinfo(host, port, &hints, &result);
	if (s)
		return -1;

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
				rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;

		close(sfd);
	}

	if (!rp)
		return -1;

	freeaddrinfo(result);

	flags = fcntl(sfd, F_GETFL);
	fcntl(sfd, F_SETFL, flags | O_NONBLOCK);

	return sfd;
}

struct obbysess *obbysess_create(const char *host, const char *port,
		int type)
{
	struct obbysess *os;
	int sock;

	/* only client is supported now */
	if (type != OSTYPE_CLIENT)
		return NULL;

	sock = __obbysess_create_client(host, port);
	if (sock == -1)
		return NULL;

	os = malloc(sizeof(struct obbysess));
	if (!os)
		return NULL;

	os->os_flags = 0;
	os->os_state = OSSTATE_OPEN;
	os->os_type = type;
	os->os_sock = sock;
	os->os_inbuf = NULL;
	os->os_outbuf = NULL;
	os->os_nitems = 0;
	os->os_eusers = 0;
	memset(&os->os_users, 0, sizeof(os->os_users));
	os->os_edocs = 0;
	memset(&os->os_docs, 0, sizeof(os->os_docs));

	os->os_notify_user = NULL;

	return os;
}

#define ARRSZ(__a) (sizeof(__a)/sizeof(*__a))

static int parse_command(struct obbysess *os, char *cmd)
{
	char *p = cmd, *q;
	int i;

	diag(os, "got command: '%s'\n", cmd);
	q = strchr(p, ':');
	if (!q)
		q = p + strlen(p);

	for (i = 0; i < ARRSZ(cmdlist); i++)
		if (!strncmp(p, cmdlist[i].oc_string, q - p))
			return cmdlist[i].oc_handler(os, q + 1);

	return -1;
}

static void parse_inbuf(struct obbysess *os)
{
	char *p = os->os_inbuf;
	char *q;
	char *cmd;
	int n;

	if (!os->os_inbuf)
		return;

	q = strchr(os->os_inbuf, '\n');
	while (q) {
		cmd = malloc(q - p + 1);
		if (!cmd) {
			os->os_state = OSSTATE_ERROR;
			return;
		}

		memcpy(cmd, p, q - p);
		cmd[q - p] = 0;

		n = parse_command(os, cmd);
		/* don't fail on unknown commands */
#if 0
		if (n < 0) {
			os->os_state = OSSTATE_ERROR;
			return;
		}
#endif

		p = q + 1;
		q = strchr(p, '\n');
	}

	cmd = *p ? strdup(p) : NULL;

	free(os->os_inbuf);
	os->os_inbuf = cmd;
}

static void send_outbuf(struct obbysess *os)
{
	if (!os->os_outbuf)
		return;

	__send(os, os->os_outbuf, strlen(os->os_outbuf));
	free(os->os_outbuf);
	os->os_outbuf = NULL;
}

void obbysess_enqueue_command(struct obbysess *os, const char *fmt, ...)
{
	va_list args;
	char *buf, *cmd;
	int n;

	va_start(args, fmt);
	n = vasprintf(&cmd, fmt, args);
	va_end(args);

	if (n < 0) {
		os->os_state = OSSTATE_ERROR;
		return;
	}

	if (os->os_outbuf) {
		buf = malloc(strlen(os->os_outbuf) + n);
		strcpy(buf, os->os_outbuf);
		strcat(buf, cmd);
		free(os->os_outbuf);
		free(cmd);
		os->os_outbuf = buf;
	} else
		os->os_outbuf = cmd;

	diag(os, "outbuf: '%s'\n", os->os_outbuf);
}

void obbysess_do(struct obbysess *os)
{
	char *buf = NULL;
	int len = 0;

	switch (os->os_state) {
		default:
			diag(os, "bad session state\n");
			return;

		case OSSTATE_NONE:
			diag(os, "session closed\n");
			return;

		case OSSTATE_ERROR:
			diag(os, "session at fault\n");
			return;

		case OSSTATE_SYNCED:
		case OSSTATE_JOINED:
		case OSSTATE_SHOOKHANDS:
		case OSSTATE_OPEN:
			break;
	}

	if (os->os_inbuf) {
		buf = strdup(os->os_inbuf);
		if (!buf) {
			os->os_state = OSSTATE_ERROR;
			return;
		}

		len = strlen(buf);
	}

	for (;;) {
		int s;
		buf = realloc(buf, len + BUFSIZ);
		if (!buf) {
			os->os_state = OSSTATE_ERROR;
			return;
		}

		s = __recv(os, buf + len, BUFSIZ);
		if (s < 0) {
			if (!len) {
				free(buf);
				buf = NULL;
			}
			break;
		}

		len += s;
		buf[len] = 0;

		if (s < BUFSIZ)
			break;
	}

	os->os_inbuf = buf;

	/* proceed to parse inbuf */
	parse_inbuf(os);

	/* send our replys */
	send_outbuf(os);
}

void obbysess_join(struct obbysess *os, const char *nick, const char *color)
{
	obbysess_enqueue_command(os, "net6_client_login:%s:%s\n", nick, color);
}

void obbysess_set_notify_callback(struct obbysess *os,
		obbysess_notify_callback_t func, void *priv)
{
	os->os_notify_user = func;
	os->os_notify_priv = priv;
}

void obbysess_destroy(struct obbysess *os)
{
	if (os->os_flags & OSFLAG_ENCRYPTED) {
		gnutls_anon_free_client_credentials(os->os_anoncred);
		gnutls_deinit(os->os_tlssess);
		gnutls_global_deinit();
	}

	if (os->os_inbuf)
		free(os->os_inbuf);
	if (os->os_outbuf)
		free(os->os_outbuf);

	obbysess_free_docs(os);
	obbysess_free_users(os);
}

