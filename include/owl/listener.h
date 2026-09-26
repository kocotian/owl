#include <owl/base.h>

#ifndef OWL_LISTENER_H
#define OWL_LISTENER_H

#include <owl/connection.h>

#define MAX_EVENTS 64

/*  Listener manages connections for a Server.
**  It is implemented asynchronously:
**  - non-blocking accept() 
**  - queue mechanism to handle multiple connections concurrently.
**  Currently only Linux' epoll is implemented.
*/

struct owl_Listener {
	struct owl_Server *server;
	int	fd;
};

int owl_listener_create(struct owl_Listener *this, struct owl_Server *server);
int owl_listener_destroy(struct owl_Listener *this);
int owl_listener_connection_put(struct owl_Listener *this, struct owl_Connection *connection);
int owl_listener_connection_get(struct owl_Listener *this, int fd, struct owl_Connection *connection);
int owl_listener_connection_del(struct owl_Listener *this, struct owl_Connection *connection);
int owl_listener_start(struct owl_Listener *this);

/* Namespace import */
# if defined(USING_NAMESPACE_OWL)
/*	Types */
	typedef struct owl_Listener Listener;

/*	Functions */
#	define listener_create owl_listener_create
#	define listener_destroy owl_listener_destroy
#	define listener_connection_put owl_listener_connection_put
#	define listener_connection_get owl_listener_connection_get
#	define listener_connection_del owl_listener_connection_del
#	define listener_start owl_listener_start
# endif

#endif
