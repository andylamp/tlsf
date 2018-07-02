/**
 * tlsf library preliminary benchmark.
 *
 * Strategy: create a decently sized pool (def. 4GB) which
 * will then be hammered by consecutive mallocs/frees of 
 * various block sizes which will be variably sized given
 * a range (def. 8kb-50mb)
 */

/**
 * Includes
 */

/* standard libraries */
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

/* tlsf lib */
#include "tlsf.h"

/* tlsf wrapper for convenience */
typedef struct _wtlsf_t  {
	tlsf_t tlsf_ptr;
	char *mem;
	size_t size;
} wtlsf_t;

/**
 * Globals
 */

// pool config
size_t min_block_size = 8192;		// default 8kb (2^13)	
size_t pool_size = 2147483648;	// default 2GB (2^31)
size_t min_pool_size = 102400;	// default is 100Mb

// request block size range
size_t blk_mul_min = 1;							// min multiplier
size_t blk_mul_max = 6400;					// max multiplier

// bench config (or 100000000)
size_t bench_trials = 100000;				// benchmark trials

/**
 * Functions
 */

/**
 * tic: function that starts the timing context
 */
clock_t
tic(char *msg) {
	if(msg != NULL) {
		printf(" ** Tick: %s\n", msg);
	}
	return clock();
}

/**
 * tock: function that takes a timing context and
 * returns the difference as a double.
 */
double
toc(clock_t start, char *msg, bool print) {
	clock_t end = clock();
	double diff = ((double)(end - start)) / CLOCKS_PER_SEC;
	if(print) {
		if(msg) {
			printf(" ** Toc (%s): Elapsed time %f seconds\n", msg, diff);
		} else {
			printf(" ** Toc: Elapsed time %f seconds\n", diff);
		}
	}
	return diff;
}

/**
 * Function that uniformly generates integer numbers 
 * within [rlow, rhigh]
 */
int 
uni_rand(int rlow, int rhigh) {
    double rval = rand()/(1.0 + RAND_MAX); 
    int range = rhigh - rlow + 1;
    int sval = (rval * range) + rlow;
    return sval;
}

/**
 * Parse console arguments
 */
bool
parse_args(int argc, char **argv) {
	int c;
	while((c = getopt(argc, argv, "t:")) != -1) {
		switch(c) {
			// handle trials argument
			case 't': {

				char *endp;
				long t_trials = strtol(optarg, &endp, 0);
				
				if(optarg != endp && *endp == '\0') {
					if(t_trials < bench_trials) {
						printf(" !! Too small trial number given (%zu), reverting to default %zu\n", t_trials, bench_trials);
					} else {
						printf(" ** Trials set to %zu\n", t_trials);
						bench_trials = (size_t) t_trials;
					}
				} else {
					printf(" !! Invalid argument supplied, reverting to default\n");
				}
				break;
			}
			default: {
				printf("uknown arg\n");
				break;
			}
		}
	}
	return true;
}

/**
 * Function that allocates memory while also warming it up,
 * so we eliminate the OS mamagement overhead.
 */
char *
alloc_mem(size_t size) {
	char *mem = NULL;
	if(size < min_pool_size) {
		printf(" !! Size was below min threshold which is %zu bytes\n", min_pool_size);
		return NULL;
	} else if((mem = calloc(sizeof(char), size)) == NULL) {
		printf(" !! Requested memory failed to yield, aborting\n");
		return NULL;
	} else {
		printf(" -- Ghostly allocated memory of size: %zu\n", size);
	}
	printf(" -- Warming up memory...\n");
	// forcefully request memory
	char *mem_addr = mem;
	for (int i = 0; i < size; ++i) {
		*mem_addr = 0;
		mem_addr++;
	}
	printf(" -- Returning warmed-up memory of size: %zu\n", size);
	return mem;
}

/**
 * This function is responsible for creating the tlsf pool structure 
 */
tlsf_t
create_tlsf_pool(wtlsf_t *pool) {
	// check for valid input
	if(pool == NULL) {
		printf(" !! No pool provided\n");
		return pool;
	} 
	// try to allocate (warm) memory
	if((pool->mem = alloc_mem(pool->size)) == NULL) {
		printf(" !! Failed to allocate and warm-up memory, cannot continue\n");
		return NULL;
	}

	// now, actually try to create tlsf
	printf(" -- Attempting to create tlsf pool of size: %zu bytes \n", pool->size);
	pool->tlsf_ptr = tlsf_create_with_pool(pool->mem, pool->size);
	if(pool->tlsf_ptr == NULL) {
		printf(" !! Failed to create tlsf pool\n");
	} else {
		printf(" -- Created a tlsf pool with size %zu bytes\n", pool->size);
	}
	return pool->tlsf_ptr;
}

/**
 * This function is responsible for destroying the tlsf pool strcture
 */
void
destroy_tlsf_pool(wtlsf_t *pool) {
	// check for valid input
	if(pool == NULL) {
		printf(" !! Cannot detroy, no valid pool provided\n");
		return;
	}
	printf(" -- Detroying tlsf pool of size %zu\n", pool->size);
	// first, destroy the pool
	if(pool->tlsf_ptr) {
		tlsf_destroy(pool->tlsf_ptr);
	}
	// then free the memory block from the OS
	if(pool->mem) {
		free(pool->mem);
	}
}

/**
 * This function generates a block of size proportional to the 
 * minimum block size by a factor randomly drawn from uniform 
 * distribution
 */
size_t
block_gen() {
	// generate a random multiplier from a uniform distribution
	int blk_mul = uni_rand(blk_mul_min, blk_mul_max);
	// generate the block size
	size_t blk = blk_mul * min_block_size;
	// return the block size
	return blk;
}

/**
 * This function benchmarks tlsf in sequential malloc/frees
 */
void
tlsf_bench(wtlsf_t *pool, int trials, int *malloc_latency, int *free_latency) {
	printf(" -- Running %d trials with a pool size of %zu bytes\n", trials, pool->size);
	clock_t ctx = tic(NULL);
	for (int i = 0; i < trials; ++i)
	{
		// generate a block size
		size_t blk_size = block_gen();
		// allocate from tlsf
		void *p1 = tlsf_malloc(pool->tlsf_ptr, blk_size);
		// free from tlsf
		tlsf_free(pool->tlsf_ptr, p1);
	}
	printf(" -- Finished %d trials in pool, elapsed time for bench was %f seconds\n", 
		trials, toc(ctx, NULL, false));
}

/**
 * Main stub
 */
int
main(int argc, char **argv) {
	printf("\n");
	// initialise the random number generator
	srand(1);
	// parse arguments
	parse_args(argc, argv);

	// generate our pool
	wtlsf_t pool;
	pool.size = pool_size;


	char *tag = "Global Pool";
	clock_t c_ctx = tic(tag);

	// create the pool
	create_tlsf_pool(&pool);
	// now run the bench
	tlsf_bench(&pool, bench_trials, NULL, NULL);
	// destroy the pool
	destroy_tlsf_pool(&pool);
	
	toc(c_ctx, tag, true);
	
	printf("\n");
  return 0;
}
