#define USING_NAMESPACE_OWL

#include <owl/listener.h>

#include <owl/server.h>
#include <owl/utils.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <stb_ds.h>

[[nodiscard]]
int
listener_create(Listener *this, Server *server)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == server) {
		errno = EINVAL;
		return -1;
	}

	this->server = server;

	/* And create an epoll instance */
	if (0 > (this->fd = epoll_create1(0)))
		/* syscall - errno is set */
		return -1;

	return 0;
}

int
listener_destroy(Listener *this)
{
	/* validate input parameters */
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	return safe_close(&(this->fd));
}

[[nodiscard]]
int
listener_register_connection(Listener *this, Connection *connection)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == connection) {
		errno = EINVAL;
		return -1;
	}

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET,
		.data.ptr = connection
	};

	if (0 > epoll_ctl(this->fd, EPOLL_CTL_ADD, connection->fd, &ev))
		/* syscall - errno is set */
		return -1;

	/* TODO: Maybe some connection list? */

	return 0;
}

[[nodiscard]]
int
listener_unregister_connection(Listener *this, Connection *connection)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == connection) {
		errno = EINVAL;
		return -1;
	}

	if (0 > epoll_ctl(this->fd, EPOLL_CTL_DEL, connection->fd, nullptr))
		/* syscall - errno is set */
		return -1;

	return 0;
}

int
listener_start(Listener *this)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == this->server) {
		errno = EINVAL;
		return -1;
	}

	/* start listening on the server's socket */
	if (0 > listen(this->server->fd, 128))
		/* syscall - errno is set */
		return -1;

#ifdef __linux__

	/* Linux implementation */
	struct epoll_event ev = {};
	struct epoll_event events[MAX_EVENTS];

	if (nullptr != this->server->ev_listening)
		if (0 > this->server->ev_listening())
			/* owl API - errno is set */
			return -1;

	/* Add server's fd for read events */
	ev.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP; /* Notify when the listening socket is ready to accept. */
	ev.data.ptr = this->server;
	if (0 > epoll_ctl(this->fd, EPOLL_CTL_ADD, this->server->fd, &ev))
		return -1;

	while (this->server->up) {
		/* Waiting for events with epoll_wait */
		int n = epoll_wait(this->fd, events, MAX_EVENTS, -1);
		if (n < 0) {
			/* Interrupted by signal, just retry */
			if (errno == EINTR || errno == EAGAIN)
				continue;

			/* Any other error, errno is set by epoll_wait */
			safe_close(&(this->fd));
			return -1;
		}

		/* Processing all events returned by epoll_wait */
		for (int i = 0; i < n; ++i) {
			if (events[i].data.ptr == this->server) {
				/* Server got a new connection */
				Connection *connection = calloc(1, sizeof *connection);
				/* Dynamic memory - should be free()'d.
				   TODO: We should think about some connection pool
				   inside Listener or somewhere. */

				if (nullptr == connection) {
					fprintf(stderr, "Failed to allocate memory for new connection: %s\n", strerror(errno));
					continue;
				}

				if (0 > connection_create(connection, this->server)) {
					free(connection);
					continue;
				}

				if (0 > listener_register_connection(this, connection)) {
					connection_destroy(connection);
					free(connection);
					continue;
				}
			} else {
				Connection *connection = events[i].data.ptr;

				if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
					if (events[i].events & EPOLLERR) {
						if (0 > listener_unregister_connection(this, connection))
							continue;
						connection_destroy(connection);
						continue;
					}
					connection_shutdown(connection);
				}

				if (connection->shutdown) {
					/* Gracefully shutdown TCP connection. */
					int rd;
					char useless[512];

					if (0 != (rd = read(connection->fd, useless, 512))) {
						if (0 > rd && errno != EAGAIN && errno != EWOULDBLOCK) {
							perror("read");
							if (0 > listener_unregister_connection(this, connection))
								continue;
							connection_destroy(connection);
						}
						continue;
					}

					/* Send event that we closed a connection. */
					if (nullptr != connection->server->ev_connection_closed)
						if (0 > connection->server->ev_connection_closed(connection))
							/* owl API - errno is set */
							return -1;

					if (0 > listener_unregister_connection(this, connection))
						continue;

					connection_destroy(connection);
					continue;
				}

				/* In this place, the connection is filled with data
				   to be read. We should read everything on one shot,
				   but it could be not a full data to be read. */

				if (nullptr != this->server->ev_connection_ready
				&&  0 > (this->server->ev_connection_ready(connection))) {
					if (0 > listener_unregister_connection(this, connection))
						continue;

					connection_destroy(connection);
					continue;
				}

				/* For now, close connection.
				   @TODO: keep-alive and track connection state
				          to know when to close when using HTTPServer.
					  Maybe with connection->should_close? */

#if 0
				connection_shutdown(connection);
				// listener_connection_destroy(this, connection);
#endif
			}
		}
	}

	safe_close(&(this->fd));
	return 0;
#else
#error "Linux implementation with epoll is required. This code is currently only supported on Linux."
#endif
}
