#define USING_NAMESPACE_OWL
#include <owl.h>

#include <stdio.h>

int
handler(HTTPTransaction *t)
{
	httpheader_push(t, "Content-Type", "text/html; charset=UTF-8");
	t->response.body = "<p>Hello, <b>world</b>!</p>\n";
	return 0;
}

int
main(void)
{
	HTTPServer server = {};
	Listener listener = {};

	if (httpserver_create(&server, "127.0.0.1", 8080) < 0) {
		perror("Failed to create server:");
		return -1;
	}

	httpserver_on_transaction(&server, handler);

	if (listener_create(&listener, (Server *)&server) < 0) {
		perror("Failed to create listener:");
		return -1;
	}
	if (listener_start(&listener) < 0) {
		perror("Failed to start listening:");
		return -1;
	}
}
