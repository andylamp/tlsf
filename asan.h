#pragma once

/* __has_feature is clang-only */
#if !defined(__has_feature)
# define __has_feature(FEATURE) 0
#endif

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>

/* Set to 1 to enable tracing of asan calls */
#if TRACE_ASAN 
#define PTR_ADD(p, x) ((void *)((uintptr_t)p + (x)))

#undef ASAN_POISON_MEMORY_REGION
#define ASAN_POISON_MEMORY_REGION(addr, size) do {       	\
	__asan_poison_memory_region((addr), (size)); 		\
	fprintf(stderr, "%s:   poison %p - %p\n",		\
		__func__, (addr), PTR_ADD((addr), (size)));	\
} while (0)

#undef ASAN_UNPOISON_MEMORY_REGION
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) do {		\
	__asan_unpoison_memory_region((addr), (size));		\
	fprintf(stderr, "%s: unpoison %p - %p\n",		\
		__func__, (addr), PTR_ADD((addr), (size)));	\
} while (0)
#endif /* TRACE_ASAN */

/* Attribute to disable address sanitizer per function */
#define ASAN_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))

#else

#define ASAN_POISON_MEMORY_REGION(addr, size)  ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define ASAN_NO_SANITIZE_ADDRESS

#endif /* address_sanitizer */
