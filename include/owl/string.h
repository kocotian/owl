#ifndef OWL_STRING_H
#define OWL_STRING_H

#include <owl/base.h>

#include <stddef.h>

struct owl_StringMut {
	char *data;
	size_t len;
	size_t cap;
};

struct owl_StringView {
	const char *data;
	size_t len;
};

int owl_str_cat(struct owl_StringMut *restrict const dest,
		const struct owl_StringView *restrict const src);
int owl_str_lock(const struct owl_StringMut *const src);

#if defined(USING_NAMESPACE_OWL) || defined(USING_OWL_STRING)
#	define StringMut owl_StringMut
#	define StringView owl_StringView

#	define str_cat owl_str_cat
#endif

#endif
