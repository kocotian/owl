#define USING_NAMESPACE_OWL

#include <owl/string.h>

#include <errno.h>
#include <string.h>

int
str_cat(struct StringMut *restrict const dest,
	struct StringView const *restrict const src)
{
	if (nullptr == dest || nullptr == src) {
		errno = EINVAL;
		return -1;
	}

	size_t avail_mem = dest->cap - dest->len - 1;

	if (avail_mem < src->len) {
		errno = ENOBUFS;
		return -1;
	}

	memcpy(dest->data + dest->len, src->data, src->len);
	dest->len += src->len;
	dest->data[dest->len] = '\0';

	return dest->len;
}

int
str_view(struct StringView *const dest, const char *const src, const size_t srclen)
{
	if (nullptr == dest || nullptr == src) {
		errno = EINVAL;
		return -1;
	}
	*dest = (struct StringView){
		.data = src,
		.len = srclen,
	};
	return 0;
}

int
str_lock(struct StringView *const dest, const struct StringMut *const src)
{
	if (nullptr == src || nullptr == dest) {
		errno = EINVAL;
		return -1;
	}
	*dest = (struct StringView){
		.data = src->data,
		.len = src->len,
	};
	return 0;
}
