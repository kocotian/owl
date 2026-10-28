#define _GNU_SOURCE

#define USING_NAMESPACE_OWL

#include <owl/connection.h>

#include <owl/arena.h>
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
connection_create(Connection *this, Server *server, struct Arena *arena)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == server || nullptr == arena) {
		errno = EINVAL;
		return -1;
	}

	/* Set to default values. */
	this->server = server;
	/* Accepting incoming connection. */
	/* TODO: We should get sockaddr. */
	this->fd = accept4(server->fd, nullptr, nullptr, SOCK_NONBLOCK);
	if (0 > this->fd)
		/* syscall - errno is set */
		return -1;

	this->arena = arena;

	/* Great. We can send event that connection is established. */
	if (nullptr != this->server->ev_connection_established)
		if (0 > this->server->ev_connection_established(this))
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
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	if (0 > shutdown(this->fd, SHUT_WR))
		/* syscall - errno is set */
		return -1;

	return (int)(this->shutdown = true);
}

int
connection_destroy(Connection *this)
{
	/* validate input parameters */
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	if (0 > this->fd) {
		/* Already closed */
		errno = EBADF;
		return -1;
	}

	int ret = safe_close(&(this->fd));

	if (0 > arena_return(this->arena)) {
		/* owl API call - errno is set */
		return -1;
	}
	/* We should invalidate entire arena,
	   because arena_return could move it. */
	this->arena = nullptr;

	/* And safely close the connection. */
	return ret;
}
