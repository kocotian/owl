#define USING_NAMESPACE_OWL

#include <owl/server.h>
#include <owl/utils.h>

#include <arpa/inet.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

[[nodiscard]]
int
server_create_unix(Server *this, const char *path)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == path) {
		errno = EINVAL;
		return -1;
	}

	/* set fd by creating and binding a socket */
	this->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (0 > this->fd)
		/* syscall - errno is set */
		return -1;

	/* initialize sockaddr_in struct for binding */
	struct sockaddr_un addr = {};
	int opt = 1;

	addr.sun_family = AF_UNIX;
	int a, b;
	if ((a = strlen(path)) != (b = strlcpy(addr.sun_path, path, sizeof (addr.sun_path)))) {
		errno = ENAMETOOLONG;
		goto cleanup;
	}

	/* bind socket to address */
	if (0 > setsockopt(this->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)))
		/* syscall - errno is set */
		goto cleanup;

	if (0 > bind(this->fd, (struct sockaddr *)&addr, sizeof (addr)))
		/* syscall - errno is set */
		goto cleanup;

	/* Set the listening server socket to non-blocking mode */
	int flags = fcntl(this->fd, F_GETFL, 0);
	if (0 > flags || 0 > fcntl(this->fd, F_SETFL, flags | O_NONBLOCK))
		/* syscall - errno is set */
		goto cleanup;

	this->up = true;

	return 0;

cleanup:
	safe_close(&(this->fd));
	return -1;
}

[[nodiscard]]
int
server_create(Server *this, const char *host, int port)
{
	struct sockaddr_in addr = {};
	int opt = 1;

	/* validate input parameters */
	if (nullptr == this || nullptr == host || port <= 0 || port >= 65536) {
		errno = EINVAL;
		return -1;
	}

	/* set fd by creating and binding a socket */
	this->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (0 > this->fd)
		/* syscall - errno is set */
		return -1;

	/* initialize sockaddr_in struct for binding */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	int inet_pton_rv;
	inet_pton_rv = inet_pton(AF_INET, host, &addr.sin_addr);
	if (0 == inet_pton_rv)
		/* inet_pton(3) says:
		   0 is returned when src does not contain valid network address...
		   -1 is returned and errno is set to EAFNOSUPPORT.
		   we must set errno on 0 return value. */
		errno = EINVAL;
	if (1 != inet_pton_rv)
		goto cleanup;

	/* bind socket to address */
	if (0 > setsockopt(this->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)))
		/* syscall - errno is set */
		goto cleanup;

	if (0 > bind(this->fd, (struct sockaddr *)&addr, sizeof (addr)))
		/* syscall - errno is set */
		goto cleanup;

	/* Set the listening server socket to non-blocking mode */
	int flags = fcntl(this->fd, F_GETFL, 0);
	if (0 > flags || 0 > fcntl(this->fd, F_SETFL, flags | O_NONBLOCK))
		/* syscall - errno is set */
		goto cleanup;

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
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	if (0 > safe_close(&(this->fd)))
		return -1;

	this->up = false;

	return 0;
}

/* Generic event setter macro */
#define SERVER_ON(EV, HANDLER)                                       \
	[[nodiscard]]                                                \
	int                                                          \
	server_on_##EV(Server *this, HANDLER)                        \
	{                                                            \
		if (nullptr == this) {                               \
		        errno = EINVAL;                              \
			return -1;                                   \
                }                                                    \
                /* don't rewrite handler if one is available.        \
                   first, null previous handler with this fn. */     \
		if (nullptr != this->ev_##EV && nullptr != handler) {\
			errno = EBUSY;                               \
			return -1;                                   \
		}                                                    \
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
