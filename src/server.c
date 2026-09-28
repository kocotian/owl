#define USING_NAMESPACE_OWL

#include <owl/server.h>
#include <owl/utils.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

[[nodiscard]]
int
server_create(Server *this, const char *host, int port)
{
	struct sockaddr_in addr = {};
	int opt = 1;

	/* validate input parameters */
	if (nullptr == this || nullptr == host || port <= 0 || port >= 65536)
		return -1;

	/* set fd by creating and binding a socket */
	this->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (0 >= this->fd) {
		printf("Failed to create socket: %s\n", strerror(errno));
		return -1;
	}

	/* initialize sockaddr_in struct for binding */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (1 != inet_pton(AF_INET, host, &addr.sin_addr)) {
		printf("Failed to convert host address: %s\n", strerror(errno));
		goto cleanup;
	}

	/* bind socket to address */
	if (0 > setsockopt(this->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt))) {
		printf("Failed to set socket options: %s\n", strerror(errno));
		goto cleanup;
	}

	if (bind(this->fd, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		printf("Failed to bind socket: %s\n", strerror(errno));
		goto cleanup;
	}

	/* Set the listening server socket to non-blocking mode */
	int flags = fcntl(this->fd, F_GETFL, 0);
	if (0 > flags || fcntl(this->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		printf("Failed to change socket mode: %s\n", strerror(errno));
		goto cleanup;
	}

	this->up = true;

	return 0;

cleanup:
	safe_close(&(this->fd));
	return -1;
}

int
server_destroy(Server *this)
{
	/* validate input parameters */
	if (nullptr == this)
		return -1;

	if (0 > safe_close(&(this->fd)))
		return -1;

	this->up = false;

	return 0;
}

/* Generic event setter macro */
#define SERVER_ON(EV, HANDLER)                                    \
	int                                                          \
	server_on_##EV(Server *this, HANDLER)                 \
	{                                                            \
		if (nullptr == this)                                 \
			return -1;                                   \
                                                                     \
                /* don't rewrite handler if one is available.        \
                   first, null previous handler with this fn. */     \
		if (nullptr != this->ev_##EV && nullptr != handler)  \
			return -2;                                   \
                                                                     \
		this->ev_##EV = handler;                             \
                                                                     \
		return 0;                                            \
	}

SERVER_ON(listening, int (*handler)(void))
SERVER_ON(connection_established, int (*handler)(Connection *connection))
SERVER_ON(connection_ready, int (*handler)(Connection *connection))
SERVER_ON(connection_closed, int (*handler)(Connection *connection))
SERVER_ON(error, int (*handler)(Connection *connection, int))
