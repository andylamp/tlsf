#include <stdio.h>
#include <stdlib.h>

#include "tlsf.h"

int
main(int argc, char **argv)
{
	size_t memory_size = 1024*1024;
	char *memory = malloc(memory_size);

	tlsf_t *tlsf = tlsf_create_with_pool(memory, memory_size);

	void *p1 = tlsf_malloc(tlsf, 100);
	void *p2 = tlsf_malloc(tlsf, 1000);
	tlsf_free(tlsf, p2);
	void *p3 = tlsf_malloc(tlsf, 1000);

	printf("Allocated %p\n", p1);
	printf("Allocated %p\n", p2);
	printf("Allocated %p\n", p3);

	tlsf_free(tlsf, p3);
	tlsf_free(tlsf, p1);

	tlsf_destroy(tlsf);

	free(memory);
}
