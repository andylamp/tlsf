#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tlsf.h"

#define TABLE_SIZE 1000000
#define ITERATIONS 10000000
#define MAX_SIZE 5000

int
main(int argc, char **argv)
{
	size_t memory_size = 1000000000;
	char *memory = malloc(memory_size);
	char ctrl[tlsf_size()];
	int oom_count = 0;

	tlsf_t *tlsf = tlsf_create(ctrl);
	pool_t *pool = tlsf_add_pool(tlsf, memory, memory_size);

	void *table[TABLE_SIZE] = { 0 };

	for (int i = 0; i < ITERATIONS; i++) {
		int j = rand() % TABLE_SIZE;
		size_t size = rand() % (MAX_SIZE + 1);
		if (table[j] != NULL) {
			memset(table[j], 0, tlsf_block_size(table[j]));
			tlsf_free(tlsf, table[j]);
		}
		void *ptr = tlsf_malloc(tlsf, size);
		if (ptr == NULL) {
			oom_count++;
		} else {
			memset(ptr, 0, size);
		}
		table[j] = ptr;

	}
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (table[i] != NULL) {
			tlsf_free(tlsf, table[i]);
			table[i] = NULL;
		}
	}

	tlsf_remove_pool(tlsf, pool);

	tlsf_destroy(tlsf);

	memset(memory, 0, memory_size);
	printf("memory: %p\n", memory);
	printf("oom count: %d\n", oom_count);

	free(memory);
}
