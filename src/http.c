#define USING_NAMESPACE_OWL

#include <owl/http.h>

#include <owl/arena.h>
#include <owl/listener.h>

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#define STBDS_NO_SHORT_NAMES
#define STB_DS_IMPLEMENTATION
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
	/* validate input parameters */
	if (nullptr == t || nullptr == key || nullptr == h) {
		errno = EINVAL;
		return -1;
	}

	auto header = stbds_shgetp_null(t->request.headers, key);
	if (nullptr == header) {
		errno = ENOENT;
		return -1;
	}

	*h = header;

	return 0;
}

int
httpheader_res_set(HTTPTransaction *t, const char *key, const char *val)
{
	/* validate input parameters */
	if (nullptr == t || nullptr == key || nullptr == val) {
		errno = EINVAL;
		return -1;
	}

	HTTPHeader h = {nullptr, nullptr};

	size_t kcap = strlen(key) + 1;
	h.key = arena_alloc(t->connection->arena, kcap);
	if (nullptr == h.key)
		return -1;

	if ((kcap - 1) != strlcpy(h.key, key, kcap))
		goto key_cleanup;

	/* TODO: surely null-terminated? */

	size_t vcap = strlen(val) + 1;
	char *value = arena_alloc(t->connection->arena, vcap);
	if ((vcap - 1) != strlcpy(value, val, vcap))
		goto val_cleanup;
	
	stbds_arrput(h.val, value);
	stbds_shputs(t->response.headers, h);

	return kcap + vcap;

val_cleanup:
	arena_free(t->connection->arena, value);
key_cleanup:
	arena_free(t->connection->arena, h.key);

	return -1;
}

int
httpheader_res_setf(HTTPTransaction *t, const char *key, const char *val_fmt, ...)
{
	/* validate input parameters */
	if (nullptr == t || nullptr == key || nullptr == val_fmt) {
		errno = EINVAL;
		return -1;
	}

	va_list args;
	int ret;
	char *val;

	va_start(args, val_fmt);
	/* TODO: implement some kind of arena_sprintf() */
	if (0 > (ret = vasprintf(&val, val_fmt, args)))
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

[[nodiscard]]
static inline bool
ispchar(char c)
{
	return (
		((c) == 0x21) || /* /!/ */
		((c) >= 0x24 && (c) <= 0x3b) || /* /$%&'()*+,-./ and /0-9/ */
		((c) == 0x3d) || /* /=/ */
		((c) >= 0x3f && (c) <= 0x5a) || /* /?/ and /A-Z/ */
		((c) == 0x5f) || /* /_/ */
		((c) >= 0x61 && (c) <= 0x7a) || /* /a-z/ */
		((c) == 0x7e) /* /~/ */
		/* See ascii(7) manual page. */
	);
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
	if (0 > fd || nullptr == st) {
		errno = EINVAL;
		return -1;
	}

	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	char *cursor = req->raw_request;
	size_t req_len_avail = req->raw_request_len;

	/* First, we are just going to check ptr[0] to determine the method,
	   to avoid unnecessary strncmp calls. Also, we check methods in order of expected frequency
	   (GET is most common, then POST/PUT, then others) to optimize for common cases. */
	if (0 != req_len_avail)
	switch (*cursor) {
	case 'G':
		/* And when first character is known, we can check the full method string. */
		if (0 == strincmp(cursor, req_len_avail, "GET", 3)) {
			req->method = HTTP_GET; cursor += 3;
		} break;
	case 'P':
		/* etc... */
		if (0 == strincmp(cursor, req_len_avail, "POST", 4)) {
			req->method = HTTP_POST; cursor += 4;
		} else if (0 == strincmp(cursor, req_len_avail, "PUT", 3)) {
			req->method = HTTP_PUT; cursor += 3;
		} else if (0 == strincmp(cursor, req_len_avail, "PATCH", 5)) {
			req->method = HTTP_PATCH; cursor += 5;
		} break;
	case 'D':
		if (0 == strincmp(cursor, req_len_avail, "DELETE", 6)) {
			req->method = HTTP_DELETE; cursor += 6;
		} break;
	case 'H':
		if (0 == strincmp(cursor, req_len_avail, "HEAD", 4)) {
			req->method = HTTP_HEAD; cursor += 4;
		} break;
	}

	req_len_avail -= (cursor - req->raw_request);

	/* And after method we expect SP */
	if (' ' == *cursor) {
		++cursor; --req_len_avail;
	} else {
		return_status (HTTP_STATUS_BAD_REQUEST);
	}

	/*
	 * Okay. If none of the above matched, we have HTTP_UNKNOWN_METHOD.
	 * According to HTTP spec, we should immediately stop parsing
	 * and respond with:
	 */
	if (HTTP_UNKNOWN_METHOD == req->method)
		return_status (HTTP_STATUS_METHOD_NOT_ALLOWED);

	/* Well. We have method, now we need to parse URI. */
	char *uri_end;
	size_t uri_len;

	uri_end = cursor;
	/* uri_end grows until SP, which marks the end of the URI. */
	while ('\0' != *uri_end && ' ' != *uri_end) {
		if (!ispchar(*uri_end))
			return_status (HTTP_STATUS_BAD_REQUEST);
		if ('%' == *uri_end && (0 == isxdigit(uri_end[1]) || 0 == isxdigit(uri_end[2])))
			return_status (HTTP_STATUS_BAD_REQUEST);
		uri_end++;
	}
	uri_len = uri_end - cursor;

	req->uri = arena_alloc(t->connection->arena, uri_len + 1);
	/* TODO: strlcpy */
	strncpy(req->uri, cursor, uri_len);
	req->uri[uri_len] = '\0';
	cursor = uri_end + 1;
	req_len_avail = req->raw_request_len - (cursor - req->raw_request);

	/* And to fulfill the request line, last thing to parse is HTTP version.
	   HTTP version is not case-insensitive. */
	if (req_len_avail < 10 || 0 != memcmp(cursor, "HTTP/", 5))
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
	while (0 != req_len_avail && 0 != istchar(*cursor = tolower(*cursor)))
		cursor++, req_len_avail--;

	/* We expect a ':' after the header name. If not present, then it's a: */
	if (':' != *cursor)
		return_status (HTTP_STATUS_BAD_REQUEST);

	/* Trying to get header if it already exists */
	header = stbds_shgetp_null(req->headers, h.key);
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

	stbds_arrput(header->val, value);
	/* If header points to h, it means that we didn't had header
	   with that key yet. */
	if (header == &h)
		stbds_shputs(req->headers, h);

	cursor += 2;
	req_len_avail -= 2;

	return 0;
}

/* Return -1 if we can't read body because of server problem (5xx)
   Return  1 ""   ""   ""   ""   ""   ""   "" client problem (4xx) */
/* TODO: just rewrite that... */
[[nodiscard]]
static int
httprequest_read_body([[maybe_unused]] int fd, HTTPConnectionState *st, char *data, size_t length)
{
	if (nullptr == st || nullptr == data || 0 == length) {
		errno = EINVAL;
		return -1;
	}

	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	HTTPHeader *content_length =
		stbds_shgetp_null(req->headers, "content-length");

	if (nullptr == content_length) {
		return 1;
	}

	/* TODO: Check if content_length have exacly one item.
	   Should move it to parser. */
	if (1 != stbds_arrlen(content_length->val))
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
	if (0 > fd || nullptr == st) {
		errno = EINVAL;
		return -1;
	}

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
		if ((0 == line_length)
		&&  offset >= (2 * (signed)(sizeof CRLF - 1))
		&&  (0 == memcmp(beginning - (sizeof  CRLF - 1),  CRLF, sizeof  CRLF - 1)
		  || 0 == memcmp(beginning - (sizeof NULLF - 1), NULLF, sizeof NULLF - 1))
		&&  end) {
			if (nullptr == req->request_line)
				return_status (HTTP_STATUS_BAD_REQUEST);
			req->body = st->cursor + 2;
		}

		if (nullptr != req->body) {
			offset = req->body - st->buffer;
			// ptrdiff_t body_buffer_offset;
			int read_body = httprequest_read_body(fd, st, req->body, st->buffer_length - offset);
			/* @TODO: temporary */
			st->done = read_body;
			return 0;
		}

		if (nullptr == end) {
			/* If CRLF was not found, it means that
			   - full line was not sent yet, so we are waiting for rest of it, or
			   - we've parsed entire header, and now it is time for reading a body.
			   We'll set `done` to true, because we've done parsing for now. */
			// st->done = 1;
			return 0;
		}

		/* If request_line is not set, we parse first line as request line. */
		if (nullptr == req->request_line) {
			int parse_request_line = httprequest_parse_request_line(fd, st);
			if (0 != parse_request_line)
				return parse_request_line;

			st->cursor = beginning + line_length + (sizeof CRLF - 1);
		/* Else, if request body is not set, we are parsing headers. */
		} else {
			int parse_header_line = httprequest_parse_header_line(st, beginning, line_length);
			if (0 != parse_header_line)
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
	if (nullptr == st || 0 > fd) {
		errno = EINVAL;
		return -1;
	}

	HTTPTransaction *t = &(st->current_transaction);
	HTTPRequest *req = &(t->request);

	ssize_t r;

	/* We will read to a buffer at most [capacity - length + <null terminator>] bytes
	   until readable. */
	while (1) {
		/* We'll check if we have at least 512 free bytes
		   in the buffer (including space for the null terminator).
		   If not, we need to realloc to read more. */
		if ((st->buffer_length + 513) >= st->buffer_capacity) {
			typeof_unqual (st->buffer) new_buffer =
				arena_realloc(t->connection->arena, st->buffer, st->buffer_capacity, st->buffer_capacity * 2);
			if (nullptr == new_buffer)
				return -1;
			st->buffer = new_buffer;
			if (!st->buffer) /* realloc failed */
				return_status (HTTP_STATUS_INTERNAL_SERVER_ERROR);
			st->buffer_capacity *= 2;
		}

		r = recv(fd, st->buffer + st->buffer_length,
			st->buffer_capacity - (st->buffer_length + 1),
			0);
		if (0 >= r)
			break;
		/* Length of data read grows by r. */
		st->buffer_length += r;

		/* If we have read more or equal to MAX_REQUEST_SIZE
		   (adding one byte for null termination),
		   we should stop reading and return an error: */
		if ((st->buffer_length + 1) >= MAX_REQUEST_SIZE)
			return_status (HTTP_STATUS_CONTENT_TOO_LARGE);
	}

	if (0 > r) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			/* An actual error occurred */
			fprintf(stderr, "[ERROR] read(fd: %d): %s\n", fd, strerror(errno));
			return_status (HTTP_STATUS_INTERNAL_SERVER_ERROR);
		}
	}

	/* If r is 0, it means the connection was closed by the client. */
	if (0 == r) {
		fprintf(stderr, "[ERROR] read(fd: %d): Connection closed by pear.\n", fd);
		/* If we haven't read any data yet, this is a... */
		if (0 == st->buffer_length) {
			fprintf(stderr, "No data read.\n");
			return_status (HTTP_STATUS_BAD_REQUEST);
		}
		/* Otherwise, just shutdown */
		/* @TODO: ADD SHUTDOWN */
		/* @TODO: IS THAT CODE BELOW ADDED SHUTDOWN THAT IT IS SAID ABOVE? */
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

	if (nullptr == st->cursor)
		st->cursor = st->buffer;

	int parse = httprequest_parse(fd, st);

	if (-100 >= parse)
		return -parse;
	if (0 > parse)
		return HTTP_STATUS_INTERNAL_SERVER_ERROR;

	return 0;
}

static inline int
xwrite(HTTPTransaction *t, const char *const restrict data, size_t outlen)
{
	HTTPResponse *res = &(t->response);

	ssize_t cldiff = res->raw_response_cap - res->raw_response_len;
	if (0 == outlen)
		outlen = strlen(data);

	if ((ssize_t)outlen < 0)
		/* TODO: temporary */
		return -1;

	if (cldiff < 64 || (ssize_t)outlen > cldiff) {
		/* 64 = sqrt(4096) */
		int old_cap = res->raw_response_cap;
		int new_cap = old_cap;
		do {
			new_cap <<= 1;
		} while (new_cap - res->raw_response_len < outlen);
		typeof_unqual (res->raw_response) new_response =
			arena_realloc(t->connection->arena, res->raw_response, old_cap, new_cap);
		if (nullptr == new_response)
			return -1;
		res->raw_response = new_response;
		res->raw_response_cap = new_cap;
	}

	if (res->raw_response_cap - res->raw_response_len < outlen)
		return -1;

	memcpy(res->raw_response + res->raw_response_len, data, outlen);
	res->raw_response_len += outlen;

	return 0;
}

static inline int
xprintf(HTTPTransaction *t, const char *fmt, ...)
{
	HTTPResponse *res = &(t->response);

	va_list ap;
	va_start(ap, fmt);
	ssize_t outlen = vsnprintf(nullptr, 0, fmt, ap);
	va_end(ap);

	if (outlen < 0)
		return -1;

	ssize_t cldiff = res->raw_response_cap - res->raw_response_len;

	if (cldiff < 64 || ((ssize_t)outlen + 1) > cldiff) {
		/* 64 = sqrt(4096) */
		int old_cap = res->raw_response_cap;
		int new_cap = old_cap;
		do {
			new_cap <<= 1;
			/* TODO: probably check if 1st bit is set (un/signed) */
		} while ((ssize_t)(new_cap - res->raw_response_len) < (outlen + 1));
		typeof_unqual (res->raw_response) new_response =
			arena_realloc(t->connection->arena, res->raw_response, old_cap, new_cap);
		if (nullptr == new_response)
			return -1;
		res->raw_response = new_response;
		res->raw_response_cap = new_cap;
	}

	/* TODO: probably check if 1st bit is set (un/signed) */
	if ((ssize_t)(res->raw_response_cap - res->raw_response_len) < outlen)
		return -1;

	va_start(ap, fmt);
	int tb = vsnprintf(res->raw_response + res->raw_response_len,
		res->raw_response_cap - res->raw_response_len, fmt, ap);
	if (tb != outlen)
		return -1;
	res->raw_response_len += outlen;
	va_end(ap);

	return tb;
}

[[nodiscard]]
int
httpresponse_prepare(HTTPTransaction *t)
{
	if (nullptr == t) {
		errno = EINVAL;
		return -1;
	}

	HTTPResponse *res = &(t->response);

	res->raw_response = arena_calloc(t->connection->arena, res->raw_response_cap = 4096);
	if (nullptr == res->raw_response) {
		res->raw_response = 
			"HTTP/1.1 503 Service Unavailable\r\n"
			"Content-Type: text/html;charset=utf-8\r\n"
			"Content-Length: 33\r\n"
			"\r\n"
			"<h1>503 Service Unavailable</h1>\n";
		res->raw_response_len = __builtin_strlen(res->raw_response);
		return res->raw_response_len;
	}

	xprintf(t, "HTTP/1.1 %d %s\r\n", t->status_code, http_status_str(t->status_code));

	size_t body_len = 0;
	if (nullptr != res->body)
		/* If there is a body, we can set body_len. */
		/* TODO: (when implementing String View?), we should
		   definitely remove strlen */
		body_len = res->content_length ?: strlen(res->body);

	/* TODO: Connection close is hardcoded, when we'll
	   make keep-alive alive, we can recreate line below
	   to decide whether connection'll be closed or not. */
	xwrite(t, "Connection: close\r\n", 0);

	for (int i = 0; i < stbds_shlen(res->headers); i++) {
		for (int j = 0; j < stbds_arrlen(res->headers[i].val); ++j)
			xprintf(t, "%s: %s\r\n", res->headers[i].key, res->headers[i].val[j]);
	}

	/* If we do not have body, we just write another CRLF. */
	if (0 == body_len)
		xwrite(t, "\r\n", 2);
	else {
		/* But if body is present, we'll append a header and send. */
		/* TODO: Check if method is not HEAD! */
		xprintf(t, "Content-Length: %zu\r\n\r\n", body_len);

		xwrite(t, res->body, body_len);
	}

	return res->raw_response_len;
}

[[nodiscard]]
int
httpresponse_write(int fd, HTTPTransaction *t)
{
	/* XXX: should be reimplemented with epoll, like _read. */

	/* validate input parameters */
	if (nullptr == t || 0 > fd) {
		errno = EINVAL;
		return -1;
	}

	HTTPResponse *res = &(t->response);

	ssize_t resp_tw = 0, resp_w;
	while ((resp_w = send(fd, res->raw_response + resp_tw,
			res->raw_response_len - resp_tw,
			MSG_NOSIGNAL)) != 0) {
		if (0 < resp_w) {
			resp_tw += resp_w;
			continue;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/* TODO: Implement EPOLLRD!!!
			   Here we are blocking a process and using 
			   enormous CPU power. */
			continue;
		} else if (errno == EINTR) {
			continue;
		} else {
			/* syscall - errno is set */
			return -1;
		}
	}
	/* TODO: probably check if 1st bit is set (un/signed) */
	if (resp_tw != (ssize_t)res->raw_response_len) {
		errno = EIO;
		return -1;
	}

	return 0;
}

[[nodiscard]]
static int
httpserver_default_connection_handler(Connection *connection)
{
	if (nullptr == connection) {
		errno = EINVAL;
		return -1;
	}

	HTTPConnectionState *st = nullptr;
	HTTPTransaction *t = nullptr;
	int status;

	/* We should first check if the connection have available state. */
	if (nullptr == connection->state) {
		/* If not - we should create one. */
		st = arena_calloc(connection->arena, sizeof *st);
		if (nullptr == st)
			return -1;
		if (0 > httpconnectionstate_create(st, (HTTPServer *)connection->server, connection)) {
			return -1;
		}
		connection->state = st;
		st->current_state = HTTPCS_REQUEST;
	} else
		/* If yes, we'll bring it back */
		st = connection->state;

	t = &(st->current_transaction);

	if (HTTPCS_REQUEST == st->current_state) {
	}

	/* TODO: destroy transaction */

	/* Read the request and parse it */
	if (0 > (status = httprequest_read(connection->fd, st))) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/* TODO: check if return 0 is good here */
			return 0;
		}
		/* Something went wrong when reading. */
		if (0 == t->status_code)
			/* Fatal error while parsing request, we should close
			** connection immediately */
			return -1;

		/* We'll respond with status code inherited from _read. */
		/* TODO: Acknowledge what did I did here */
	} else {
		/* We have got a status code when parsing. We should respond now. */
		if (300 <= status) {
			t->status_code = status;

			/* Handle error */
			if (0 != t->server->ev_status_code)
				t->server->ev_status_code(t);

		/* If parsing is not done yet, do nothing. */
		} else {
			if (0 == st->done)
				return 0;

			/* Successfully parsed request, generate normal response. */
			t->status_code = HTTP_STATUS_OK;
		}

		t->request.response = &(t->response);

		/* If everything went good, we can begin transaction */
		if (nullptr != t->server->ev_transaction)
			/* @TODO: if line below fails, we should make some error */
			t->server->ev_transaction(t);
		else
			t->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR; /* Internal Server Error */
	}

	if (0 > httpresponse_prepare(t))
		return -1;

	if (0 > httpresponse_write(connection->fd, t))
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
httpserver_create_unix(HTTPServer *this, const char *path)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == path) {
		errno = EINVAL;
		return -1;
	}

	if (0 > server_create_unix((Server *)this, path))
		return -2;

	/* default HTTP handlers */
	if (0 > server_on_connection_ready((Server *)this, httpserver_default_connection_handler))
		return -3;

	return 0;
}

int
httpserver_create(HTTPServer *this, const char *host, int port)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == host || port <= 0 || port >= 65536) {
		errno = EINVAL;
		return -1;
	}

	if (0 > server_create((Server *)this, host, port))
		return -1;

	/* default HTTP handlers */
	if (0 > server_on_connection_ready((Server *)this, httpserver_default_connection_handler))
		return -1;

	return 0;
}

int
httptransaction_create(HTTPTransaction *this, HTTPServer *server, Connection *connection)
{
	/* validate input parameters */
	if (nullptr == this || nullptr == server) {
		errno = EINVAL;
		return -1;
	}

	/* Assign original server */
	this->server = server;
	this->connection = connection;

	return 0;
}

int
httpconnectionstate_create(struct owl_HTTPConnectionState *this, struct owl_HTTPServer *server, Connection *connection)
{
	/* validate input parameters */
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	if (0 > httptransaction_create(&(this->current_transaction), server, connection))
		return -1;

	/* Then get data for request to be read.
	   TODO: Probably rewrite. */
	if (nullptr == (this->buffer = arena_alloc(connection->arena, this->buffer_capacity = 1024)))
		return -1;

	this->buffer_length = 0;
	this->connection = connection;

	return 0;
}

/* Generic event setter macro */
#define HTTPSERVER_ON(EV, HANDLER)                                   \
	int                                                          \
	httpserver_on_##EV(HTTPServer *this, HANDLER)                \
	{                                                            \
		if (nullptr == this) {                               \
			errno = EINVAL;                              \
			return -1;                                   \
		}                                                    \
                                                                     \
                /* You can't rewrite handler if one is available,    \
                   as it will return an error.                       \
                   First, null previous handler with this fn. */     \
		if (nullptr != this->ev_##EV && nullptr != handler) {\
			errno = EBUSY;                               \
			return -1;                                   \
                }                                                    \
                                                                     \
		this->ev_##EV = handler;                             \
                                                                     \
		return 0;                                            \
	}

HTTPSERVER_ON(transaction, int (*handler)(HTTPTransaction *))
