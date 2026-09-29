#define _GNU_SOURCE

#define USING_NAMESPACE_OWL

#include <owl/connection.h>

#include <owl/server.h>
#include <owl/listener.h>
#include <owl/utils.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdlib.h>

[[nodiscard]]
int
connection_create(Connection *this, Server *server)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == server)
		return -1;

	/* Set to default values. */
	this->server = server;
	/* Accepting incoming connection. */
	/* TODO: We should get sockaddr. */
	this->fd = accept4(server->fd, nullptr, nullptr, SOCK_NONBLOCK);
	if (this->fd < 0)
		return -1;

	/* Great. We can send event that connection is established. */
	if (this->server->ev_connection_established)
		if (this->server->ev_connection_established(this) < 0)
			goto cleanup;

	/* We had listener_add_connection().
	   We replaced it with listener_connection_create().
	   Maybe we should go back to listener_add_connection()?
	   TODO. */

	return 0;

cleanup:
	safe_close(&(this->fd));
	return -1;
}

int
connection_shutdown(Connection *this)
{
	/* validate input parameters */
	if (nullptr == this)
		return -1;

	if (0 > shutdown(this->fd, SHUT_WR))
		return -1;

	return (int)(this->shutdown = true);
}

int
connection_destroy(Connection *this)
{
	/* validate input parameters */
	if (nullptr == this)
		return -1;

	if (this->fd < 0) {
		/* Already closed */
		return -1;
	}

	/* XXX: is that call below a good idea? */
	shutdown(this->fd, SHUT_RD);

	/* And safely close the connection. */
	return safe_close(&(this->fd));
}
