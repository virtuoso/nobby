
#ifndef __COBBY_H__

enum {
	OSTYPE_NONE = 0,
	OSTYPE_CLIENT,
	OSTYPE_SERVER,
};

enum {
	OSSTATE_NONE = 0,
	OSSTATE_OPEN,
	OSSTATE_SHOOKHANDS,
	OSSTATE_JOINED,
	OSSTATE_SYNCED,
	OSSTATE_ERROR,
};

#define OSFLAG_ENCRYPTED (0x1)

#define MAX_USERS 256

struct obbyuser {
	char *ou_name;
	unsigned long ou_color;
	char *ou_net6uid;
	unsigned ou_obbyuid;
	unsigned ou_enctyped;
};

#define MAX_DOCS 1024

struct obbydoc {
	char *od_name;
	char *od_encoding;
	unsigned od_obbyuid;
	unsigned od_obbyuididx;
	unsigned od_nusers;
};

struct obbyevent {
	int oe_type;
	char *oe_docname;
	char *oe_username;
	char *oe_message;
	long oe_length;
	/* to be extended */
};

enum {
	OETYPE_NONE = 0,
	OETYPE_USER_KNOWN,
	OETYPE_USER_JOINED,
	OETYPE_USER_PARTED,
	OETYPE_DOC_KNOWN,
	OETYPE_DOC_OPEN,
	OETYPE_DOC_GETCHUNK,
	OETYPE_CHAT_MESSAGE,
	OETYPE_DEBUG_MESSAGE,
};

typedef int (*obbysess_notify_callback_t)(void *, struct obbyevent *);

#define obbysess_notify(__os, __type, __args...) \
	do { \
		struct obbyevent __oe = { .oe_type = __type, ## __args }; \
		if (__os->os_notify_user) \
			__os->os_notify_user(__os->os_notify_priv, &__oe); \
	} while (0);

struct obbysess {
	int os_sock;
	int os_type;
	int os_state;
	unsigned os_flags;
	int os_proto;
	char *os_inbuf;
	char *os_outbuf;
	gnutls_session_t os_tlssess;
	gnutls_anon_client_credentials_t os_anoncred;

	int os_nitems; /* scratch: number of entries */
	int os_eusers; /* number of users known to us */
	struct obbyuser *os_users[MAX_USERS];

	int os_edocs;  /* number of docs */
	struct obbydoc *os_docs[MAX_DOCS];

	/* user's callback */
	obbysess_notify_callback_t os_notify_user;
	void *os_notify_priv;
};

#define OS_ISOK(__os) ((__os)->os_state != OSSTATE_ERROR)

char *obby_escape_string(const char *input, int replace);
char *obby_unescape_string(const char *input, int replace);

struct obbysess *obbysess_create(const char *host, const char *port,
		int type);
void obbysess_destroy(struct obbysess *os);

void obbysess_set_notify_callback(struct obbysess *os,
		obbysess_notify_callback_t func, void *priv);

void obbysess_do(struct obbysess *os);

void obbysess_join(struct obbysess *os, const char *nick, const char *color);

void obbysess_enqueue_command(struct obbysess *os, const char *fmt, ...);

#endif /* __COBBY_H__ */

