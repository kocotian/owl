#include <owl/base.h>

#ifndef OWL_CONNECTION_H
#define OWL_CONNECTION_H

#include <owl/arena.h>

/* Connection is an object responsible for managing a connection. */

struct owl_Connection {
	struct owl_Server *server;
	int	fd;
	enum {
		CONN_CLIENT_SHUT_RD = 1 << 0,
		CONN_CLIENT_SHUT_WR = 1 << 1,
		CONN_SERVER_SHUT_RD = 1 << 2,
		CONN_SERVER_SHUT_WR = 1 << 3,
	} sockpair_state;
	bool	shutdown;
	bool	fin;
	void	*state;
	struct Arena *arena;
};

/* On object's create(), connection is accept()'ed from a server
   and an event about establishing connection is send. */
[[nodiscard]] int owl_connection_create(struct owl_Connection *this,
		struct owl_Server *server, struct owl_Arena *arena);

int owl_connection_shutdown(struct owl_Connection *this);

/* On object's destroy(), we first send event that connection is
   closed, and then safely close one. */
int owl_connection_destroy(struct owl_Connection *this);

/* Namespace import */
# if defined(USING_NAMESPACE_OWL)
/*	Types */
	typedef struct owl_Connection Connection;

/*	Functions */
#	define connection_create owl_connection_create
#	define connection_shutdown owl_connection_shutdown
#	define connection_destroy owl_connection_destroy
# endif

#endif
