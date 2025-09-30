/* Force-include this header to enable mimalloc's malloc redirection when
   mimalloc is installed. This file is safe to include even when mimalloc
   headers are not present. */
#ifndef ROBUSTEXT_MIMALLOC_FORCE_H
#define ROBUSTEXT_MIMALLOC_FORCE_H

#ifdef __has_include
#if __has_include(<mimalloc.h>)
#define MI_MALLOC_REDIRECT
#include <mimalloc.h>
#endif
#endif

#endif /* ROBUSTEXT_MIMALLOC_FORCE_H */
