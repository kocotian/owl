#include <owl/base.h>

#ifndef OWL_UTILS_H
#define OWL_UTILS_H

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int safe_close(int *fd);

#endif
