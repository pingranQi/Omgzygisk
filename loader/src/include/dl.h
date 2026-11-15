#ifndef ONYX_DL_H
#define ONYX_DL_H

#include <dlfcn.h>

#ifdef __cplusplus
extern "C" {
#endif

void *DlopenMem(int memfd, int flags);

#ifdef __cplusplus
}
#endif

#endif
