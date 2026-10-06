#include <owl/base.h>

#ifndef OWL_SERVER_H
#define OWL_SERVER_H

/*  Server structure represents a network server that listens for incoming
**  connections and manages them.
**
**  This is base Server struct that should be inherited by different types of
**  servers (like HTTP, FTP, SMTP, WebSocket, etc.) by inheriting it in specific
**  server structs.
**
**  By default Server holds fd and connections.
**  Also, Server holds event handlers defined for miscelaneous actions like
**  listening, connection, etc.
*/

#include <owl/connection.h>

struct owl_Server {
	int	fd;
	bool	up;
	int	(*ev_listening)();
	int	(*ev_connection_established)(struct owl_Connection *);
	int	(*ev_connection_ready)(struct owl_Connection *);
	int	(*ev_connection_closed)(struct owl_Connection *);
	int	(*ev_error)(struct owl_Connection *, int);
};

int owl_server_create_unix(struct owl_Server *this, const char *path);
int owl_server_create(struct owl_Server *this, const char *host, int port);
int owl_server_destroy(struct owl_Server *this);

int owl_server_on_listening(struct owl_Server *this, int (*handler)());
int owl_server_on_connection_established(struct owl_Server *this, int (*handler)(struct owl_Connection *));
int owl_server_on_connection_ready(struct owl_Server *this, int (*handler)(struct owl_Connection *));
int owl_server_on_connection_closed(struct owl_Server *this, int (*handler)(struct owl_Connection *));
int owl_server_on_error(struct owl_Server *this, int (*handler)(struct owl_Connection *, int));

/* Namespace import */
# if defined(USING_NAMESPACE_OWL)
/*	Types */
	typedef struct owl_Server Server;

/*	Functions */
#	define server_create_unix owl_server_create_unix
#	define server_create owl_server_create
#	define server_destroy owl_server_destroy
#	define server_on_listening owl_server_on_listening
#	define server_on_connection_established owl_server_on_connection_established
#	define server_on_connection_ready owl_server_on_connection_ready
#	define server_on_connection_close owl_server_on_connection_close
#	define server_on_error owl_server_on_error
# endif

#endif
