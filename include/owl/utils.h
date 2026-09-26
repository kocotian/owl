#include <owl/base.h>

#ifndef OWL_UTILS_H
#define OWL_UTILS_H

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define PP_CAT3(a,b,c) PP_CAT3_I(a,b,c)
#define PP_CAT3_I(a,b,c) a##b##c
#define __uniq__ PP_CAT3(_$uniq,__LINE__,$_)

int uri_decode(const char *encoded, char **decoded, int form_decode);
char **explode(const char delim, const char *str, int n);
int safe_close(int *fd);

#endif
