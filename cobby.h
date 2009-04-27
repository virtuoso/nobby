
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
};

#define OS_ISOK(__os) ((__os)->os_state != OSSTATE_ERROR)

void obby_setdbgfn(void (*fn)(const char *, ...));
void obby_setmsgfn(void (*fn)(const char *, ...));
struct obbysess *obbysess_create(const char *host, const char *port,
		int type);
void obbysess_destroy(struct obbysess *os);

void obbysess_do(struct obbysess *os);

void obbysess_join(struct obbysess *os, const char *nick, const char *color);

void obbysess_enqueue_command(struct obbysess *os, const char *fmt, ...);

#endif /* __COBBY_H__ */

