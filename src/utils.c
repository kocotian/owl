#include <owl/utils.h>

#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include <stb_ds.h>

/*
 * This function takes an encoded URI (e.g. "/path/to/resource%20with%20spaces?query=string")
 * and decodes it into the provided decoded buffer, which should be large enough to hold the result.
 *
 * If decoded is nullptr, it allocates a new buffer on the heap which the caller is responsible for freeing.
 * If form_decode is non-zero, it also decodes '+' characters into spaces, as per application/x-www-form-urlencoded rules.
 *
 * Returns:
 * - length of the decoded URI on success (not including null terminator)
 * - negative value on error (e.g. invalid percent-encoding)
 */

/* TODO: Rewrite, maybe move to HTTP */
int
uri_decode(const char *encoded, char **decoded, int form_decode)
{
	size_t len = strlen(encoded);
	size_t j = 0;
	char *buf = *decoded ? *decoded : malloc(len + 1); /* decoded URI can't be longer than encoded */

	for (size_t i = 0; i < len; i++) {
		if (encoded[i] == '%' && i + 2 <= len && isxdigit(encoded[i + 1]) && isxdigit(encoded[i + 2])) {
			char ch = (char)((isdigit(encoded[i + 1]) ? (encoded[i + 1] - '0') : (tolower(encoded[i + 1]) - 'a' + 10)) * 16 +
					 (isdigit(encoded[i + 2]) ? (encoded[i + 2] - '0') : (tolower(encoded[i + 2]) - 'a' + 10)));
			buf[j++] = ch;
		} else if (form_decode && encoded[i] == '+') {
			buf[j++] = ' ';
		} else {
			buf[j++] = encoded[i];
		}
	}
	buf[j] = '\0';
	if (!*decoded)
		*decoded = buf;
	return j;
}

char **
explode(const char delim, const char *str, int n)
{
	char **result;
	char *dup, *ptr;
	int i;

	result = nullptr;
	ptr = dup = strdup(str);
	i = 0;

	if (n <= 0)
		n = strlen(str);

	while (i < n && dup[i]) {
		if (dup[i] == delim) {
			dup[i] = '\0';
			arrput(result, ptr);
			ptr = dup + i + 1;
		}
		++i;
	}
	arrput(result, ptr);
	return result;
}

int
safe_close(int *fd)
{
	if (nullptr == fd)
		return -1;

	if (*fd < 0)
		return 0;

	int ret = close(*fd);
	if (ret == 0)
		*fd = -1;

	return ret;
}
