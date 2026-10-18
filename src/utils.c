#include <owl/utils.h>

#include <errno.h>
#include <unistd.h>

int
safe_close(int *fd)
{
	if (nullptr == fd) {
		errno = EINVAL;
		return -1;
	}

	if (0 > *fd)
		return 0;

	int ret = close(*fd);
	if (0 == ret)
		*fd = -1;

	return ret;
}
