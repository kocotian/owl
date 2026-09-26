#include <owl/utils.h>

#include <unistd.h>

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
