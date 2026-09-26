#define USING_NAMESPACE_OWL

#include <owl/http.h>

#include <owl/listener.h>

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include <stb_ds.h>

/* 8 MB max request size for now, to avoid unbounded memory usage. */
#define MAX_REQUEST_SIZE (8 * 1024 * 1024)
#define MAX_URI_LENGTH (8 * 1024)
#define CRLF "\r\n"
#define NULLF "\0\n"

char *
http_status_str(int status_code)
{
	switch (status_code) {
	case HTTP_STATUS_OK: return "OK";
	case HTTP_STATUS_BAD_REQUEST: return "Bad request";
	case HTTP_STATUS_NOT_FOUND: return "Not found";
	case HTTP_STATUS_METHOD_NOT_ALLOWED: return "Method not allowed";
	case HTTP_STATUS_NOT_IMPLEMENTED: return "Not implemented";
	case HTTP_STATUS_INTERNAL_SERVER_ERROR: return "Internal server error";
	case HTTP_STATUS_CONTENT_TOO_LARGE: return "Content too large";
	default: return "";
	}
}

char *
http_method_str(HTTPMethod method)
{
	switch (method) {
	case HTTP_GET: return "GET";
	case HTTP_HEAD: return "HEAD";
	case HTTP_POST: return "POST";
	case HTTP_PUT: return "PUT";
	case HTTP_DELETE: return "DELETE";
	case HTTP_PATCH: return "PATCH";
	default: return "UNKNOWN";
	}

	/* Unreachable */
	return nullptr;
}

static int
istchar(int c) /* returns tchar or 0 if not a tchar */
{
    unsigned char uc = (unsigned char)c;
    if (uc >= '0' && uc <= '9') return uc;
    if (uc >= 'A' && uc <= 'Z') return uc;
    if (uc >= 'a' && uc <= 'z') return uc;
    switch (uc) {
        case '!': case '#': case '$': case '%': case '&': case '\'':
        case '*': case '+': case '-': case '.': case '^': case '_':
        case '`': case '|': case '~':
            return uc;
        default:
            return 0;
    }
}

static int
isvchar(int c) /* returns vchar or 0 if not a vchar */
{
    unsigned char uc = (unsigned char)c;
    return (uc >= 0x20 && uc <= 0x7E) ? uc : 0;
}

int
httpheader_req_get(HTTPTransaction *t, const char *key, HTTPHeader **h)
{
	if (nullptr == t || nullptr == key || nullptr == h)
		return -1;

	if (nullptr == (*h = shgetp_null(t->request.headers, key)))
		return -1;

	return 0;
}

int
httpheader_res_set(HTTPTransaction *t, const char *key, const char *val)
{
	/* TODO: harden, change malloc to arena when implemented */
	HTTPHeader h = {nullptr, nullptr};
	size_t cap;
	if (nullptr == (h.key = malloc(cap = strlen(key) + 1)))
		return -1;

	if ((cap - 1) != strlcpy(h.key, key, cap)) {
		free(h.key);
		return -1;
	}

	/* TODO: surely null-terminated? */
	char *value = malloc(cap = strlen(val) + 1);
	if ((cap - 1) != strlcpy(value, val, cap)) {
		free(h.val);
		free(h.key);
		return -1;
	}
	
	arrput(h.val, value);
	shputs(t->response.headers, h);

	return strlen(key) + strlen(val) + 2;
}

int
httpheader_res_setf(HTTPTransaction *t, const char *key, const char *val_fmt, ...)
{
	va_list args;
	int ret;
	char *val;

	va_start(args, val_fmt);
	if ((ret = vasprintf(&val, val_fmt, args)) < 0)
		val = nullptr;
	va_end(args);

	ret = httpheader_push(t, key, val);
	free(val);

	return ret;
}

#define return_status(CODE) return -(t->status_code = CODE)

#define strlcmp(s1, s2, n1, n2) ((n1) - (n2) ? (n1) - (n2) : strncmp(s1, s2, n1))
#define strincmp(str, slen, sub, sublen) ((sublen <= slen) ? strncmp(str, sub, sublen) : (signed)(sublen - slen))
#define strlcasecmp(s1, s2, n1, n2) ((n1) - (n2) ? (n1) - (n2) : strncasecmp(s1, s2, n1))
#define strincasecmp(str, slen, sub, sublen) ((sublen <= slen) ? strncasecmp(str, sub, sublen) : sublen - slen)

#define ISPCHAR(C) (\
		((C) == 0x21) || /* /!/ */\
		((C) >= 0x24 && (C) <= 0x3b) || /* /$%&'()*+,-./ and /0-9/ */\
		((C) == 0x3d) || /* /=/ */\
		((C) >= 0x3f && (C) <= 0x5a) || /* /?/ and /A-Z/ */\
		((C) == 0x5f) || /* /_/ */\
		((C) >= 0x61 && (C) <= 0x7a) || /* /a-z/ */\
		((C) == 0x7e) /* /~/ */\
		/* See ascii(7) manual page. */\
	)

[[maybe_unused]]
static bool
ispchar(char c)
{
	return ISPCHAR(c);
}

[[nodiscard]]
static int
httprequest_parse_request_line(int fd, HTTPConnectionState *st)
{
	/* Should be called only when a CRLF-terminated response line is present
	   and ready to be parsed. */

	/* RFC 9112, Section 3:
	   A request-line begins with a method token, followed by a single
	   space (SP), the request-target, and another single space (SP), and
	   ends with the protocol version.

	   request-line   = method SP request-target SP HTTP-version

	   Although the request-line grammar rule requires that each of the
	   component elements be separated by a single SP octet, recipients MAY
	   instead parse on whitespace-delimited word boundaries and, aside
	   from the CRLF terminator,
	   >> TREAT ANY FORM OF WHITESPACE AS THE SP SEPARATOR WHILE IGNORING
	   >> PRECEDING OR TRAILING WHITESPACE; SUCH WHITESPACE INCLUDES ONE OR
	   >> MORE OF THE FOLLOWING OCTETS: SP, HTAB, VT (%X0B), FF (%X0C), OR
	   >> BARE CR. */

	/* validate input parameters */
	if (nullptr == st || fd < 0)
		return -1;

	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	char *cursor = req->raw_request;
	size_t req_len_avail = req->raw_request_len;

	/* First, we are just going to check ptr[0] to determine the method,
	   to avoid unnecessary strncmp calls. Also, we check methods in order of expected frequency
	   (GET is most common, then POST/PUT, then others) to optimize for common cases. */
	if (req_len_avail)
	switch (*cursor) {
	case 'G':
		/* And when first character is known, we can check the full method string. */
		if (strincmp(cursor, req_len_avail, "GET", 3) == 0)
			(req->method = HTTP_GET), cursor += 3;
		break;
	case 'P':
		/* etc... */
		if (strincmp(cursor, req_len_avail, "POST", 4) == 0)
			(req->method = HTTP_POST), cursor += 4;
		else if (strincmp(cursor, req_len_avail, "PUT", 3) == 0)
			(req->method = HTTP_PUT), cursor += 3;
		else if (strincmp(cursor, req_len_avail, "PATCH", 5) == 0)
			(req->method = HTTP_PATCH), cursor += 5;
		break;
	case 'D':
		if (strincmp(cursor, req_len_avail, "DELETE", 6) == 0)
			(req->method = HTTP_DELETE), cursor += 6;
		 break;
	case 'H':
		if (strincmp(cursor, req_len_avail, "HEAD", 4) == 0)
			(req->method = HTTP_HEAD), cursor += 4;
		break;
	}

	req_len_avail -= (cursor - req->raw_request);

	/* And after method we expect SP */
	if (*cursor == ' ')
		++cursor, --req_len_avail;
	else
		return_status (HTTP_STATUS_BAD_REQUEST);

	/*
	 * Okay. If none of the above matched, we have HTTP_UNKNOWN_METHOD.
	 * According to HTTP spec, we should immediately stop parsing
	 * and respond with:
	 */
	if (req->method == HTTP_UNKNOWN_METHOD)
		return_status (HTTP_STATUS_METHOD_NOT_ALLOWED);

	/* Well. We have method, now we need to parse URI. */
	char *uri_end;
	size_t uri_len;

	uri_end = cursor;
	/* uri_end grows until SP, which marks the end of the URI. */
	while (*uri_end && *uri_end != ' ') {
		if (!ISPCHAR(*uri_end))
			return_status (HTTP_STATUS_BAD_REQUEST);
		if (*uri_end == '%' && !(isxdigit(uri_end[1]) && isxdigit(uri_end[2])))
			return_status (HTTP_STATUS_BAD_REQUEST);
		uri_end++;
	}
	uri_len = uri_end - cursor;

	req->uri = malloc(uri_len + 1);
	/* TODO: strlcpy */
	strncpy(req->uri, cursor, uri_len);
	req->uri[uri_len] = '\0';
	cursor = uri_end + 1;
	req_len_avail = req->raw_request_len - (cursor - req->raw_request);

	/* And to fulfill the request line, last thing to parse is HTTP version.
	   HTTP version is not case-insensitive. */
	if (req_len_avail < 10 || memcmp(cursor, "HTTP/", 5) != 0)
		return_status (HTTP_STATUS_BAD_REQUEST);

	/* For now, we don't expect HTTP/10.0 or HTTP/3.10, so we just check
	 * for single digit major and minor versions. */
	/* @TODO: According to specification, we MUST be prepared for HTTP/10.0 and HTTP/1.10 */
	if (!isdigit(cursor[5]) || cursor[6] != '.' || !isdigit(cursor[7])
	||  cursor[8] != '\r' || cursor[9] != '\n')
		return_status (HTTP_STATUS_BAD_REQUEST);

	req->version[0] = cursor[5] - '0';
	req->version[1] = cursor[7] - '0';
	cursor += 10;
	req_len_avail -= 10;

	/* Request line is parsed. */

	req->request_line = cursor;
	return 0;
}

[[nodiscard]]
static int
httprequest_parse_header_line(HTTPConnectionState *st, char *line, size_t line_length)
{
	/* @TODO: - allow multiple headers (HTTPHeader.val changes from
		    (char *) -> (char **)
		  - harden */
	/* Should be called only when a CRLF-terminated header line is present
	   and ready to be parsed. */

	/* Header name is not case-sensitive, but we will store them lowercase.
	   Header value is case-sensitive. */

	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	char *cursor = line;
	size_t req_len_avail = line_length;

	HTTPHeader h = {nullptr, nullptr};
	HTTPHeader *header = nullptr;
	char *value = nullptr;

	h.key = cursor;
	/* cursor grows while we have tchar characters, which are valid in header names. */
	while (req_len_avail && istchar(*cursor = tolower(*cursor)) != 0)
		cursor++, req_len_avail--;

	/* We expect a ':' after the header name. If not present, then it's a: */
	if (*cursor != ':')
		return_status (HTTP_STATUS_BAD_REQUEST);

	/* Trying to get header if it already exists */
	header = shgetp_null(req->headers, h.key);
	/* If header is still nullptr, it means that header with that key
	   do not exists. */
	if (nullptr == header)
		header = &h;

	/* TODO: - check for headers like Content-Type, Content-Length, etc. */

	*cursor++ = '\0', req_len_avail--;
	while (req_len_avail && isblank(*cursor)) /* skip optional whitespace(s) after ':' */
		/* @TODO: check if there can be more than single space, and should it be trailed */
		cursor++, req_len_avail--;

	value = cursor;
	while (req_len_avail && isvchar(*cursor) != 0)
		cursor++, req_len_avail--;

	*cursor = '\0';

	arrput(header->val, value);
	/* If header points to h, it means that we didn't had header
	   with that key yet. */
	if (header == &h)
		shputs(req->headers, h);

	cursor += 2;
	req_len_avail -= 2;

	return 0;
}

[[nodiscard]]
static int
httprequest_read_body(int fd, HTTPConnectionState *st, char *data, size_t length)
{
	(void)fd; (void)data;

	if (nullptr == st || (nullptr == data && length != 0))
		return -1;

	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	HTTPHeader *content_length =
		shgetp_null(req->headers, "content-length");

	if (nullptr == content_length) {
		return 1;
	}

	/* TODO: Check if content_length have exacly one item.
	   Should move it to parser. */
	if (arrlen(content_length->val) != 1)
		return -1;

	size_t clen = atol(content_length->val[0]);

	if (length < clen)
		return 0;

	req->body[clen] = '\0';

	return 1;
}

/* This function is responsible for parsing the HTTP request into a structured
 * format. Allocates memory for the fields in provided Request struct.
 *
 * Returns:
 * - Negative return value means that an error happened.
 *   When return code is -100 and below, it means we
 *   return HTTP status code.
 * - Positive return value means that it couldn't be
 *   processed yet, so we should try again next time.
 * - Zero when everything is done.
 */

[[nodiscard]]
static int
httprequest_parse(int fd, HTTPConnectionState *st)
{
	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	/* Let's begin parsing.
	   ptr will be our cursor as we parse through request. */
	char *beginning = nullptr, *end = nullptr;

	ptrdiff_t offset = 0;
	ptrdiff_t line_length = 0;

	st->done = 0;

	while (1) {
		/* Well, first let's get offset between cursor and beginning of a buffer.
		   That offset grows after every CRLF, so in other words it is a difference
		   between beginning of current line and beginning of request buffer.
		   st->cursor is moved only when full line was received (and parsed approprietly). */

		/* On every loop iteration, it is beginning of data to be parsed. */
		beginning = st->cursor;

		/* And it is an offset of beginning from a buffer (position) */
		offset = beginning - st->buffer;

		/* Then, we are going to find nearest CRLF. */
		end = memmem(st->cursor, st->buffer_length - offset, CRLF, sizeof CRLF - 1);

		/* If CRLF is present, we should calculate offset between end (CRLF)
		   and beginning of a line: length of it. */
		line_length = (end ? end - beginning : 0);

		/* If line is empty, offset is greater than singular CRLF/NULLF length and
		   there is CRLF of NULLF(^1) exactly before current line,
		   entire header was theoretically parsed, so we can set pointer to a body.
		   
		   (^1) - Parsing is done in destructive way - When parsing eg. headers, we will
		   change \r to \0, so we have null-terminated header value. We should check that!
		   NULLF is "\0\n", as defined above. */
		if (!line_length
		&&  offset >= (2 * (signed)(sizeof CRLF - 1))
		&&  (!memcmp(beginning - (sizeof  CRLF - 1),  CRLF, sizeof  CRLF - 1)
		  || !memcmp(beginning - (sizeof NULLF - 1), NULLF, sizeof NULLF - 1))
		&&  end) {
			if (!req->request_line)
				return_status (HTTP_STATUS_BAD_REQUEST);
			req->body = st->cursor + 2;
		}

		if (req->body) {
			offset = req->body - st->buffer;
			// ptrdiff_t body_buffer_offset;
			int read_body = httprequest_read_body(fd, st, req->body, st->buffer_length - offset);
			/* @TODO: temporary */
			st->done = read_body;
			return 0;
		}

		if (!end) {
			/* If CRLF was not found, it means that
			   - full line was not sent yet, so we are waiting for rest of it, or
			   - we've parsed entire header, and now it is time for reading a body.
			   We'll set `done` to true, because we've done parsing for now. */
			// st->done = 1;
			return 0;
		}

		/* If request_line is not set, we parse first line as request line. */
		if (!req->request_line) {
			int parse_request_line = httprequest_parse_request_line(fd, st);
			if (parse_request_line)
				return parse_request_line;

			st->cursor = beginning + line_length + (sizeof CRLF - 1);
		/* Else, if request body is not set, we are parsing headers. */
		} else {
			int parse_header_line = httprequest_parse_header_line(st, beginning, line_length);
			if (parse_header_line)
				return parse_header_line;
		/* If we have a request line and body is set, we should read body. */
		}

		st->cursor = end + 2;
	}

	return 0;
}

/*
 * This function is responsible for reading data from the socket.
 *
 * TODO:
 * The caller is responsible for freeing heap memory allocated here
 * using httprequest_free() after response is sent (or request is not needed anymore).
 *
 * Returns:
 * - 0 if everything was read successfully
 * - HTTP status code (e.g. 400, 413, 500) if there was an error parsing the request
 * - negative value if there was a fatal error (e.g. read error, memory allocation failure)
 *   and the connection should be closed immediately (perhaps after sending a 500 response
 *   if possible).
 */
[[nodiscard]]
int
httprequest_read(int fd, HTTPConnectionState *st)
{
	if (nullptr == st || fd < 0)
		return -1;

	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	ssize_t r;

	/* We will read to a buffer at most [capacity - length + <null terminator>] bytes
	   until readable. */
	while ((r = read(fd, st->buffer + st->buffer_length, st->buffer_capacity - (st->buffer_length + 1))) > 0) {
		/* Length of data read grows by r. */
		st->buffer_length += r;

		/* If we have read more or equal to MAX_REQUEST_SIZE
		   (adding one byte for null termination),
		   we should stop reading and return an error: */
		if ((st->buffer_length + 1) >= MAX_REQUEST_SIZE)
			return_status (HTTP_STATUS_CONTENT_TOO_LARGE);

		/* If we don't have any more space in the buffer (including space for the null terminator),
		   we need to realloc to read more. */
		if ((st->buffer_length + 1) >= st->buffer_capacity) {
			st->buffer = realloc(st->buffer, st->buffer_capacity *= 2);
			if (!st->buffer) /* realloc failed */
				return_status (HTTP_STATUS_INTERNAL_SERVER_ERROR);
		}
	}

	if (r < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
		/* An actual error occurred */
		fprintf(stderr, "[ERROR] read(fd: %d): %s\n", fd, strerror(errno));
		return_status (HTTP_STATUS_INTERNAL_SERVER_ERROR);
	}

	/* If r == 0, it means the connection was closed by the client. */
	if (r == 0) {
		fprintf(stderr, "[ERROR] read(fd: %d): Connection closed by pear.\n", fd);
		/* If we haven't read any data yet, this is a... */
		if (st->buffer_length == 0) {
			fprintf(stderr, "No data read.\n");
			return_status (HTTP_STATUS_BAD_REQUEST);
		}
		/* Otherwise, just shutdown */
		/* @TODO: ADD SHUTDOWN */
		connection_shutdown(st->connection);
		return 0;
	}

	/*
	 * We have read all available data into raw_request, now we null-terminate it.
	 */
	if ((st->buffer_length + 1) >= st->buffer_capacity)
		return_status (HTTP_STATUS_INTERNAL_SERVER_ERROR);

	st->buffer[st->buffer_length] = '\0';

	req->raw_request = st->buffer;
	req->raw_request_len = st->buffer_length;
	req->raw_request_cap = st->buffer_capacity;

	if (!st->cursor)
		st->cursor = st->buffer;

	int parse = httprequest_parse(fd, st);

	if (parse <= -100)
		return -parse;
	if (parse < 0)
		return HTTP_STATUS_INTERNAL_SERVER_ERROR;

	return 0;
}

[[nodiscard]]
int
httpresponse_write(int fd, HTTPTransaction *t)
{
	/* XXX: should be reimplemented with epoll, like _read */

	/* validate input parameters */
	if (nullptr == t || fd < 0)
		return -1;

	HTTPResponse *res = &(t->response);

	/* total written bytes and last write */
	ssize_t tw, w;
	size_t body_len = 0;
	int i, j; /* iterator */

	/* initialization of tw with status line */
	tw = dprintf(fd, "HTTP/1.1 %d %s\r\n", t->status_code, http_status_str(t->status_code));
	if (tw < 0) {
		   if (errno != EAGAIN && errno != EWOULDBLOCK)
			return -1;
	}

	/* TODO: (... && res->content_length > 0) */
	if (nullptr == res->body)
		/* If there is no body, body_len becomes 0. */
		body_len = 0;
	else
		/* Otherwise, we can set body_len */
		body_len = res->content_length ?: strlen(res->body);
	httpheader_pushf(t, "Content-Length", "%zu", body_len);

#if 0
	/* keep connection alive if client sent appropriate header */
	if (nullptr == shgetp_null(t->request.headers, "connection")
	||  strncmp(shgets(t->request.headers, "connection").val, "keep-alive", strlen("keep-alive")))
		httpheader_pushf(t, "Connection", "close");
	else
		httpheader_pushf(t, "Connection", "keep-alive");
#else
	httpheader_pushf(t, "Connection", "close");
#endif

	/* headers will initialize w and add to tw */
	for (i = 0; i < shlen(res->headers); i++) {
		/* @todo: BUG stopping server immediately without any message
		   when holding F5 in a browser. strace says that it just
		   gets SIGPIPE. 

		   @update: probably fixed */

		for (j = 0; j < arrlen(res->headers[i].val); ++j) {
			w = dprintf(fd, "%s: %s\r\n", res->headers[i].key, res->headers[i].val[j]);
			if (w < 0) {
				if (errno != EAGAIN && errno != EWOULDBLOCK)
					return -1;
				--i; /* retry this header */
			}
			tw += w;
		}
	}

	if ((w = write(fd, "\r\n", 2)) != 2)
		return -1;

	if (nullptr != res->body) {
		ssize_t body_tw = 0, body_w;
		/* Only if body is present */
		while ((body_w = write(fd, res->body + body_tw, body_len - body_tw)) != 0) {
			if (body_w > 0)
				body_tw += body_w;
			else if (errno != EAGAIN && errno != EWOULDBLOCK)
				continue;
			else {
				return -1;
				/* TODO: some error */
			}
		}
		if (body_tw != (ssize_t)body_len)
			return -1;

		w = body_tw;
	}

	tw += w + 2;

	return 0;
}

[[nodiscard]]
static int
httpserver_default_connection_handler(Connection *connection)
{
	if (nullptr == connection)
		return -1;

	HTTPConnectionState *st = nullptr;
	HTTPTransaction *t = nullptr;
	int status;

	if (!connection->state) {
		st = calloc(1, sizeof *st);
		httpconnectionstate_create(st, (HTTPServer *)connection->server);
		st->connection = connection;
		connection->state = st;
	} else
		st = connection->state;

	t = &(st->current_transaction);

	/* TODO: destroy transaction */

	/* Read the request and parse it */
	if ((status = httprequest_read(connection->fd, st)) < 0) {
		/* Something went wrong when reading. */
		if (!t->status_code)
			/* Fatal error while parsing request, we should close
			** connection immediately */
			return -1;

		/* We'll respond with status code inherited from _read. */
		/* TODO: Acknowledge what did I did here */
	} else {
		/* We have got a status code when parsing. We should respond now. */
		if (status >= 300) {
			t->status_code = status;

			/* Handle error */
			if (t->server->ev_status_code)
				t->server->ev_status_code(t);

		/* If parsing is not done yet, do nothing. */
		} else {
			if (!st->done)
				return 0;

			/* Successfully parsed request, generate normal response. */
			t->status_code = HTTP_STATUS_OK;
		}

		t->request.response = &(t->response);

		/* If everything went good, we can begin transaction */
		if (t->server->ev_transaction)
			/* @TODO: if line below fails, we should make some error */
			t->server->ev_transaction(t);
		else
			t->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR; /* Internal Server Error */
	}

	if (httpresponse_write(connection->fd, t) < 0)
		/* Maybe returning some error? */
		return -1;

	/* If connection is keep alive, don't do shit below */
	connection_shutdown(st->connection);
	connection_destroy(st->connection);

#if 0
	/* @TODO */
	httptransaction_destroy(&t);
#endif

	return 0;
}

int
httpserver_create(HTTPServer *this, const char *host, int port)
{
	/* validate input parameters */
	if (!this || !host || port <= 0 || port >= 65536)
		return -1;

	if (server_create((Server *)this, host, port) < 0)
		return -2;

	/* default HTTP handlers */
	if (server_on_connection_ready((Server *)this, httpserver_default_connection_handler) < 0)
		return -3;

	return 0;
}

int
httptransaction_create(HTTPTransaction *this, HTTPServer *server)
{
	if (!this || !server)
		return -1;

	/* Assign original server */
	this->server = server;

	return 0;
}

int
httpconnectionstate_create(struct owl_HTTPConnectionState *this, struct owl_HTTPServer *server)
{
	if (!this)
		return -1;

	if (httptransaction_create(&(this->current_transaction), server) < 0)
		return -1;

	/* Then get data for request to be read.
	   TODO:
	   - Should be arena-allocated when arena allocator will be written.
	   - Probably will be rewritten at all. */
	if (nullptr == (this->buffer = malloc(this->buffer_capacity = 1024)))
		return -1;

	this->buffer_length = 0;

	return 0;
}

/* Generic event setter macro */
#define HTTPSERVER_ON(EV, HANDLER)                                   \
	int                                                          \
	httpserver_on_##EV(HTTPServer *this, HANDLER)         \
	{                                                            \
		if (nullptr == this)                                 \
			return -1;                                   \
                                                                     \
                /* You can't rewrite handler if one is available,    \
                   as it will return an error.                       \
                   First, null previous handler with this fn. */     \
		if (nullptr != this->ev_##EV && nullptr != handler)  \
			return -2;                                   \
                                                                     \
		this->ev_##EV = handler;                             \
                                                                     \
		return 0;                                            \
	}

HTTPSERVER_ON(transaction, int (*handler)(HTTPTransaction *))
