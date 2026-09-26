#include <owl/base.h>

#ifndef OWL_HTTP_H
#define OWL_HTTP_H

#include <owl/server.h>

#include <stddef.h>

enum owl_HTTPMethod;
enum owl_HTTPStatuscode;
struct owl_HTTPRequest;
struct owl_HTTPResponse;
struct owl_HTTPTransaction;
struct owl_HTTPHeader;
struct owl_HTTPServer;

enum owl_HTTPMethod {
	HTTP_UNKNOWN_METHOD  = 0,
	HTTP_GET             = 1 << 0,
	HTTP_HEAD            = 1 << 1,
	HTTP_POST            = 1 << 2,
	HTTP_PUT             = 1 << 3,
	HTTP_DELETE          = 1 << 4,
	HTTP_PATCH           = 1 << 8,
};

enum owl_HTTPStatuscode {
	HTTP_STATUS_OK = 200,
	HTTP_STATUS_BAD_REQUEST = 400,
	HTTP_STATUS_NOT_FOUND = 404,
	HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
	HTTP_STATUS_CONTENT_TOO_LARGE = 413,
	HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
	HTTP_STATUS_NOT_IMPLEMENTED = 501,
};

struct owl_HTTPRequest {
	/* Raw request (entire payload). */
	char *raw_request;
	size_t raw_request_len, raw_request_cap;

	char *cursor;
	bool is_parsing_done;

	/* Pointers to raw request (with length) */
	char *request_line;	size_t request_line_len;
	char *raw_headers;	size_t raw_headers_len;
	char *body;		size_t body_len;

	/* Parsed data: */
	enum owl_HTTPMethod method;
	char *uri; /* encoded URI as received in the request line, before percent-decoding */
	int version[2];
	struct owl_HTTPHeader *headers;

	char *resource; /* decoded URI, after percent-decoding resource_encoded */
	char *query; /* query string part of the URI, without the '?' */

	/* TODO: do wyjebania: */
	struct owl_HTTPResponse *response;
};

struct owl_HTTPResponse {
	struct owl_HTTPHeader *headers;

	char *body;
	size_t content_length;
};

struct owl_HTTPTransaction {
	struct owl_HTTPServer *server;

	struct owl_HTTPRequest request;
	struct owl_HTTPResponse response;

	int status_code;
	int request_done;
};

struct owl_HTTPHeader {
	char *key;
	char **val;
};

struct owl_HTTPConnectionState {
	struct owl_Connection *connection;
	struct owl_HTTPTransaction current_transaction;
	int done;

	char *buffer;
	size_t buffer_capacity, buffer_length;
	
	char *cursor;
};

struct owl_HTTPServer /* inherits owl_Server */ {
	struct owl_Server base;

	int (*ev_transaction)(struct owl_HTTPTransaction *);
	int (*ev_status_code)(struct owl_HTTPTransaction *);
	struct {
		struct owl_Connection *key;
		struct owl_HTTPTransaction *val;
	} ctmap;
};

char *owl_http_status_str(int status_code);
char *owl_http_method_str(enum owl_HTTPMethod method);

int owl_httpheader_req_get(struct owl_HTTPTransaction *t, const char *key, struct owl_HTTPHeader **h);
int owl_httpheader_req_set(struct owl_HTTPTransaction *t, const char *key, const char *val);
int owl_httpheader_res_get(struct owl_HTTPTransaction *t, const char *key, struct owl_HTTPHeader **h);
int owl_httpheader_res_set(struct owl_HTTPTransaction *t, const char *key, const char *val);
int owl_httpheader_res_setf(struct owl_HTTPTransaction *t, const char *key, const char *val_fmt, ...);
int owl_httprequest_read(int fd, struct owl_HTTPConnectionState *st);
int owl_httpresponse_write(int fd, struct owl_HTTPTransaction *t);
int owl_httpserver_create(struct owl_HTTPServer *this, const char *host, int port);
int owl_httptransaction_create(struct owl_HTTPTransaction *this, struct owl_HTTPServer *server);
int owl_httpconnectionstate_create(struct owl_HTTPConnectionState *this, struct owl_HTTPServer *server);

int owl_httpserver_on_transaction(struct owl_HTTPServer *, int (*handler)(struct owl_HTTPTransaction *));

/* Namespace import */
# if defined(USING_NAMESPACE_OWL)

/*	Types */
	typedef enum owl_HTTPMethod HTTPMethod;
	typedef enum owl_HTTPStatuscode HTTPStatuscode;
	typedef struct owl_HTTPRequest HTTPRequest;
	typedef struct owl_HTTPResponse HTTPResponse;
	typedef struct owl_HTTPTransaction HTTPTransaction;
	typedef struct owl_HTTPConnectionState HTTPConnectionState;
	typedef struct owl_HTTPHeader HTTPHeader;
	typedef struct owl_HTTPServer HTTPServer;

/*	Functions */
#	define http_status_str owl_http_status_str
#	define http_method_str owl_http_method_str

#	define httpheader_req_get owl_httpheader_req_get
#	define httpheader_req_set owl_httpheader_req_set
#	define httpheader_res_get owl_httpheader_res_get
#	define httpheader_res_set owl_httpheader_res_set
#	define httpheader_res_setf owl_httpheader_res_setf
#	define httprequest_read owl_httprequest_read
#	define httpresponse_write owl_httpresponse_write
#	define httpserver_create owl_httpserver_create
#	define httptransaction_create owl_httptransaction_create
#	define httpconnectionstate_create owl_httpconnectionstate_create

#	define httpserver_on_transaction owl_httpserver_on_transaction

/*	Compatibility */
#	define httpheader_push owl_httpheader_res_set
#	define httpheader_pushf owl_httpheader_res_setf
# endif

#endif
