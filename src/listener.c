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

/* 32 kB of initial memory per connection */
#define MEMORY_CHUNKSIZ 32 * 1024

int
listener_create(Listener *this, Server *server)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == server)
		return -1;

	this->server = server;

	/* And create an epoll instance */
	if ((this->fd = epoll_create1(0)) < 0)
		return -1;

	return 0;
}

int
listener_destroy(Listener *this)
{
	return safe_close(&(this->fd));
}

int
listener_connection_create(Listener *this)
{
	/* validate input parameters */
	if (nullptr == this)
		return -1;

	/* To get a connection we'll do a simple trick:
	   Every connection has own memory pool.
	   Connection cannot live on stack, because we are
	   giving a pointer to it to a listener (epoll) instance.
	   That way, we will allocate connection's memory first,
	   and then we'll put that connection as a first element
	   in that memory. */

	/* We must check if connection even fits into our memory. */
	static_assert (sizeof (Connection) <= MEMORY_CHUNKSIZ);

	/* Then we can allocate memory. */
	char *memory = calloc(1, MEMORY_CHUNKSIZ);

	if (nullptr == memory)
		/* We failed to create new connection. */
		return -1;

	Connection *connection = (Connection *)memory;
	/* We did calloc, so connection should be 0'ed */

	if (connection_create(connection, this->server) != 0) {
		/* When creation failed, we should free allocation */
		free(connection);
		return -1;
	}

	/* And we must set ->memory to point to itself. */
	connection->memory = memory;
	/* TODO: change normal malloc to some arena allocator? */

	if (epoll_ctl(this->fd, EPOLL_CTL_ADD, connection->fd, &(struct epoll_event){
			.events = EPOLLIN | EPOLLET,
			.data.ptr = connection
		}) < 0)
		return -1;

	return 0;
}

int
listener_connection_destroy(Listener *this, Connection *connection)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == connection)
		return -1;

	if (connection->fin == false) {
#if 0
		if (connection->shutdown == false) {
			connection_shutdown(connection);
			return 0;
		}
#endif
		return 0;
	}
	
	if (epoll_ctl(this->fd, EPOLL_CTL_DEL, connection->fd, nullptr) < 0)
		return -1;

	if (connection_destroy(connection) < 0)
		return -1;

	free(connection->memory);

	return 0;
}

int
listener_start(Listener *this)
{
	/*  ignore SIGPIPE to prevent crashes
	**  when writing to closed sockets.
	**  @TODO: handle EPIPE errors on all write()
	**  and dprintf()'s
	**  @TODO: maybe changing read() to recv()
	**  and write() to send()? and implementation
	**  of network printf() as a dprintf() with
	**  send() instead of write()?
	*/  signal(SIGPIPE, SIG_IGN);

	/* validate input parameters */
	if (nullptr == this || nullptr == this->server)
		return -1;

	/* start listening on the server's socket */
	if (listen(this->server->fd, 128) < 0) {
		return -2;
	}

#ifdef __linux__

	/* Linux implementation */
	struct epoll_event ev = {};
	struct epoll_event events[MAX_EVENTS];

	if ((this->server->ev_listening)
	&&  (this->server->ev_listening() < 0))
		return -1;

	/* Add server's fd for read events */
	ev.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP; /* Notify when the listening socket is ready to accept. */
	ev.data.ptr = this->server;
	if (epoll_ctl(this->fd, EPOLL_CTL_ADD, this->server->fd, &ev) < 0)
		return -1;

	while (this->server->up) {
		/* Waiting for events with epoll_wait */
		int n = epoll_wait(this->fd, events, MAX_EVENTS, -1);
		if (n < 0) {
			/* Interrupted by signal, just retry */
			if (errno == EINTR || errno == EAGAIN)
				continue;

			/* Any other error */
			safe_close(&(this->fd));
			return -1;
		}

		/* Processing all events returned by epoll_wait */
		for (int i = 0; i < n; ++i) {
			if (events[i].data.ptr == this->server) {
				/* Server got a new connection */
				if (listener_connection_create(this) < 0)
					break;
			} else {
				Connection *connection = events[i].data.ptr;

				if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
					if (events[i].events & EPOLLERR) {
						connection_destroy(connection);
						continue;
					}
					connection_shutdown(connection);
				}

				if (connection->shutdown == true) {
					/* Gracefully shutdown TCP connection. */
					int rd;
					char useless[512];

					if ((rd = read(connection->fd, useless, 512)) != 0) {
						if (rd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
							perror("read");
							connection->fin = true;
							connection_destroy(connection);
						}
						continue;
					}

					connection->fin = true;

					/* Send event that we closed a connection. */
					if (connection->server->ev_connection_closed)
						if (connection->server->ev_connection_closed(connection) < 0)
							return -1;

					connection_destroy(connection);
					continue;
				}

				/* In this place, the connection is filled with data
				   to be read. We should read everything on one shot,
				   but it could be not a full data to be read. */

				if ((this->server->ev_connection_ready)
				&&  (this->server->ev_connection_ready(connection) < 0)) {
					listener_connection_destroy(this, connection);
					continue;
				}

				/* For now, close connection.
				   @TODO: keep-alive and track connection state
				          to know when to close when using HTTPServer.
					  Maybe with connection->should_close? */

#if 0
				connection_shutdown(connection);
				listener_connection_destroy(this, connection);
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
