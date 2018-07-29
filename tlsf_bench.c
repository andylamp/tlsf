/**
 * tlsf library preliminary benchmark.
 *
 * Strategy: create a decently sized pool (def. 4GB) which
 * will then be hammered by consecutive mallocs/frees of 
 * various block sizes which will be variably sized given
 * a range (def. 8kb-50mb)
 *
 * 
 * @author Andreas Grammenos <axorl@niometrics.com>
 * @copyright Copyright (c) 2018, Niometrics
 * All rights reserved.
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
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
// sys includes
#include <sys/types.h>
#include <sys/stat.h>
// intrinsics
#include <x86intrin.h>

/* tlsf lib */
#include "tlsf.h"

/* tlsf wrapper for convenience */
typedef struct _wtlsf_t  {
  tlsf_t *tlsf_ptr;
  char *mem;
  size_t size;
} wtlsf_t;

/* allocation plan types */
typedef enum _alloc_plan_type_t {
  ALLOC_SEQ = 0,
  ALLOC_RAMP = 1,
  ALLOC_HAMMER = 2,
  ALLOC_CUSTOM = 3,
  /* maybe more to be added? */
} alloc_plan_type_t;

/* allocation slot types */
typedef enum _slot_type_t {
  SLOT_EMPTY = 0,
  SLOT_MALLOC = 1,
  SLOT_FREE = 2,
  /* possibly expand? */
} slot_type_t;

/* allocation plan helper */
typedef struct _alloc_plan_t {
  // essentials
  // half sized arrays (as many as the allocs)
  char **mem_ptr;           // memory block pointers
  int *malloc_tag_time;     // memory block tag time
  size_t *cur_malloc_size;  // current malloc size
  // full sized arrays
  size_t *block_id;         // the block id
  size_t *block_size;       // memory block actual size
  slot_type_t *slot_type;   // flag to show if it's for malloc or free
  // timing stuff
  double *timings;          // timings for each op performed in the plan
  // misc. stuff
  // statistics
  size_t peak_alloc;        // peak allocation for this plan 
  size_t aggregated_alloc;  // aggregated allocation for this plan
  // plan size
  size_t plan_size;         // the plan size -- should be <= pool_size/2
  // allocation plan type
  alloc_plan_type_t type;   // the plan type
} alloc_plan_t;

/**
 * Globals
 */
const size_t mb_div = (1024*1024); // divider from Bytes to MB 

// pool config
size_t min_block_size = 8192;   // default 8kb (2^13) 
size_t pool_size = 2147483648;  // default 2GB (2^31)
size_t min_pool_size = 102400;  // default is 100Mb

// request block size range
size_t blk_mul_min = 1;             // min multiplier
size_t blk_mul_max = 6400;          // max multiplier

// allocation configuration
size_t def_trail = 10;          // default trail size

// bench config (or 100000000)
size_t min_trials = 1000;       // min benchmark trials
size_t bench_trials = 10000000; // benchmark trials

// file format details
int line_offset = 2;    // line offset

// trace dump related
char *dump_dir = "./traces";                    // dump directory
char *dump_ext = "csv";                         // dump extension
char *dump_mem_trace_suffix = "mem_trace_out";  // suffix for mem trace

// tokenizer related
char *tok_delim_cm = ",";
char *tok_delim_nl = "\n";
bool parsing_out_traces = false;

/**
 * Functions
 */

/**
 * tic: function that starts the timing context
 */
unsigned long long
tic(char *msg) {
  if(msg != NULL) {
    printf(" ** Tick (%s)\n", msg);
  }
  return __builtin_ia32_rdtsc();
}

/**
 * tock: function that takes a timing context and
 * returns the difference as a double.
 */
double
toc(unsigned long long start, char *msg, bool print) {
  unsigned long long end = __builtin_ia32_rdtsc();
  unsigned long long diff = ((end - start)); /// CLOCKS_PER_SEC;
  if(print) {
    if(msg) {
      printf(" ** Toc (%s): Elapsed time %llu cycles\n", msg, diff);
    } else {
      printf(" ** Toc: Elapsed time %llu cycles\n", diff);
    }
  }
  return diff;
}

/**
 * Function that uniformly generates integer numbers 
 * within (rlow, rhigh]
 */
int 
uni_rand(int rlow, int rhigh) {
    double rval = rand()/(1.0 + RAND_MAX); 
    int range = rhigh - rlow + 1;
    int sval = (rval * range) + rlow;
    return sval;
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
 * This function generates a sequential type of allocation plan.
 */
size_t
tag_seq(alloc_plan_t *plan, size_t trail_size) {
  if(plan->plan_size % 2 != 0 || 
    plan->plan_size % trail_size != 0) {
    printf(" !! Cannot create allocation plan, plan size but be multiple \
      of trail size and even\n");
    return 0;
  } else {
    printf(" ** Plan size is %zu using a trail size of %zu\n", 
      plan->plan_size, trail_size);
  }
  // loop once and tag allocations
  size_t blk_size = 0;
  size_t total_alloc_size = 0;
  size_t cur_alloc_size = 0;
  size_t peak_alloc = 0;
  int mem_alloc = 0;
  for (int i = 0; i < plan->plan_size; i += 2*trail_size) {
    for(int j = i; j < i+trail_size; j++) {
      blk_size = block_gen();
      cur_alloc_size += blk_size;
      // tag malloc block
      //printf(" -- Tagging malloc block at: %d with size of %zu bytes\n", j, blk_size);
      // now tag allocation blocks
      plan->block_size[j] = blk_size;       // tag the block size
      plan->slot_type[j] = SLOT_MALLOC;     // tag the slot type, 'malloc'
      plan->malloc_tag_time[mem_alloc] = j; // tag the plan time for this malloc
      plan->block_id[j] = mem_alloc;
      // now tag deallocation blocks
      plan->block_size[j+trail_size] = blk_size;  // index in mem array instead of size
      plan->slot_type[j+trail_size] = SLOT_FREE;  // tag slot type, 'free'
      plan->block_id[j+trail_size] = mem_alloc;
      mem_alloc++;
    }
    // add to aggregate
    total_alloc_size += cur_alloc_size;
    // check for peak
    if(peak_alloc < cur_alloc_size) {
      peak_alloc = cur_alloc_size;
    }
    cur_alloc_size = 0;
  }
  // update plan details
  plan->aggregated_alloc = total_alloc_size;
  plan->peak_alloc = peak_alloc;
  // sanity check
  assert(mem_alloc == plan->plan_size/2);
  // return the total allocation size for this plan
  return total_alloc_size;
}

/**
 * This function tags blocks using a ramp-type of plan
 */
size_t
tag_ramp(alloc_plan_t *plan) {
  //TODO
  return 0;
}

/**
 * This function tags blocks using a hammer-type of plan
 */
size_t
tag_hammer(alloc_plan_t *plan) {
  //TODO
  return 0;
}

/**
 * Tags memory blocks with specific memory size allocations
 *
 * ALLOC_SEQ: Sequential trails of allocs/deallocs
 *
 * ALLOC_RAMP: All allocations done first (ramp-phase), then all deallocs are performed
 *
 * ALLOC_HAMMER: All allocations are performed in pairs of alloc/dealloc
 */
void
tag_blocks(alloc_plan_t *plan) {
  size_t total_alloc_size = 0;

  // check plan types
  switch(plan->type) {
    case ALLOC_SEQ: {
      printf(" ** Tagging blocks with allocation plan: SEQUENTIAL\n");
      total_alloc_size = tag_seq(plan, def_trail);
      break;
    }
    case ALLOC_RAMP: {
      printf(" ** Tagging blocks with allocation plan: RAMP\n");
      total_alloc_size = tag_ramp(plan);
      break;
    }
    case ALLOC_HAMMER: {
      printf(" ** Tagging blocks with allocation plan: HAMMER\n");
      total_alloc_size = tag_hammer(plan);
      break;
    }
    default: {
      printf(" ** Tagging blocks with (default) allocation plan: SEQUENTIAL\n");
      plan->type = ALLOC_SEQ;
      total_alloc_size = tag_seq(plan, def_trail);
      break;
    }
  }
  // total allocation size should not be zero
  assert(total_alloc_size > 0);
  printf(" ** Final tags: %zu out of %zu; Total plan pressure: %zu MB, \
peak plan allocation: %zu MB\n", 
    plan->plan_size/2, plan->plan_size, 
    total_alloc_size/mb_div, plan->peak_alloc/mb_div);
}

/**
 * Combined preallocation for allocation plan
 */
bool
perform_plan_prealloc(alloc_plan_t *plan) {
  size_t plan_size = plan->plan_size;
  if(plan_size % 2 != 0) {
    printf(" !! Error, plan size must be even and contain as many allocs as deallocs\n");
    return false;
  }
  // block size array
  if((plan->block_size = calloc(plan_size, sizeof(size_t))) == NULL) {
    printf(" !! Failed to allocate block size\n");
    return false;
  }
  // timings array
  if((plan->timings = calloc(plan_size, sizeof(double))) == NULL) {
    printf(" !! Failed to allocate timings array\n");
    free(plan->block_size);
    return false;
  }
  // slot type array
  if((plan->slot_type = calloc(plan_size, sizeof(slot_type_t))) == NULL) {
    printf(" !! Failed to allocate slot type array\n");
    free(plan->block_size);
    free(plan->timings);
    return false;
  }
  // cur_malloc_size array
  if((plan->cur_malloc_size = calloc(plan_size / 2, sizeof(size_t))) == NULL) {
    printf(" !! Failed to allocate cur malloc size array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    return false;
  }
  // mem_ptr
  if((plan->mem_ptr = calloc(plan_size / 2, sizeof(char *))) == NULL) {
    printf(" !! Failed to allocate pointer array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    free(plan->cur_malloc_size);
    return false;
  }
  // malloc tag time
  if((plan->malloc_tag_time = calloc(plan_size / 2, sizeof(int))) == NULL) {
    printf(" !! Failed to allocate malloc tag time array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    free(plan->cur_malloc_size);
    free(plan->mem_ptr);
    return false;
  }
  // block id
  if ((plan->block_id = calloc(plan_size, sizeof(size_t))) == NULL) {
    printf(" !! Failed to allocate block id array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->slot_type);
    free(plan->cur_malloc_size);
    free(plan->mem_ptr);
    free(plan->malloc_tag_time);
    return false;
  }

  printf(" ** Preallocated successfully a plan of size %zu\n", plan_size);
  return true;
}


/**
 * This function generates an allocation plan based on
 * current trial size; we also make sure that the allocation
 * plan *does not* exceed maximum pool size at any given point.
 *
 * If no plan can be generated the user is informed and false is
 * returned to indicate failure
 *
 * NOTE: There have to be as many allocs as deallocs
 */
bool
gen_alloc_plan(size_t plan_size, alloc_plan_t *plan) {
  // basic error checks
  if(plan_size < min_trials) {
    printf(" !! Not enough trials, cannot continue (given: %zu, min req: %zu)\n", 
      plan_size, min_trials);
    return false;
  } else if(plan_size < 2) {
    printf(" !! Cannot have a plan size < 2 provided was: %zu\n", plan_size);
    return false;
  } else if(plan_size % 2 != 0) {
    printf(" !! Cannot have an odd plan size, given %zu \n", plan_size);
    return false;
  } else if(plan == NULL) {
    printf(" !! Null plan struct provided, cannot continue\n");
    return false;
  }
  // erase the structure
  memset(plan, 0, sizeof(*plan));
  // set the plan size
  plan->plan_size = plan_size;

  // first of all, preallocate required vars
  if(!perform_plan_prealloc(plan)) {
    return false;
  }

  // tag the blocks
  tag_blocks(plan);

  // finally return true
  return true;
}

/**
 * This function is responsible for destroying the allocation plan
 */
void
destroy_alloc_plan(alloc_plan_t *plan) {
  // check if we have a valid pointer
  if(plan == NULL) {
    printf(" !! No valid plan provided, cannot continue\n");
    return;
  }
  // now check if we have some addresses
  if(plan->mem_ptr != NULL) {
    printf(" -- Valid memory block pointer array found, freeing\n");
    free(plan->mem_ptr); 
    plan->mem_ptr = NULL;
  }
  // check if we have a malloc tag array
  if(plan->malloc_tag_time != NULL) {
    printf(" -- Valid malloc tag array found, freeing\n");
    free(plan->malloc_tag_time);
    plan->malloc_tag_time = NULL;
  }
  // check if we have a block id array
  if(plan->block_id != NULL) {
    printf(" -- Valid block_id array found, freeing\n");
    free(plan->block_id);
    plan->block_id = NULL;
  }
  // check if we have a cur malloc size type array
  if (plan->cur_malloc_size != NULL) {
    printf(" -- Valid cur malloc size array found, freeing\n");
    free(plan->cur_malloc_size);
    plan->cur_malloc_size = NULL;
  }
  // check if we have a valid block size type array
  if(plan->block_size != NULL) {
    printf(" -- Valid block size array found, freeing\n");
    free(plan->block_size);
    plan->block_size = NULL;
  }
  // check if we have a valid allocation type array
  if(plan->slot_type != NULL) {
    printf(" -- Valid allocation type array found, freeing\n");
    free(plan->slot_type);
    plan->slot_type = NULL;
  }
  // check if we have a valid timings array
  if(plan->timings != NULL) {
    printf(" -- Valid timings array found, freeing\n");
    free(plan->timings);
    plan->timings = NULL;
  }
}

/**
 * Parse console arguments
 */
bool
parse_args(int argc, char **argv) {
  int c;
  while((c = getopt(argc, argv, "t:p:")) != -1) {
    switch(c) {
      // parse plan size, if supplied
      case 't': {

        char *endp;
        long t_trials = strtol(optarg, &endp, 0);
        if(optarg != endp && *endp == '\0') {
          if(t_trials < min_trials) {
            printf(" !! Too small trial number given (%zu), reverting to default %zu\n", 
              t_trials, min_trials);
          } else {
            printf(" ** Trials set to %zu\n", t_trials);
            bench_trials = (size_t) t_trials;
          }
        } else {
          printf(" !! Invalid argument supplied, reverting to default\n");
        }
        break;
      }
      // parse custom plan from file, if supplied
      case 'p': {
        //TODO
        break;
      }
      default: {
        printf("unknown arg\n");
        break;
      }
    }
  }
  return true;
}

/**
 * Function that allocates memory while also warming it up,
 * so we eliminate the OS management overhead.
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
tlsf_t *
create_tlsf_pool(wtlsf_t *pool, size_t size) {
  // check for valid input
  if(pool == NULL) {
    printf(" !! No pool provided\n");
    return NULL;
  } 
  // check for valid size
  if(size < min_pool_size) {
    printf(" !! Pool must be at least of size %zu and requested: %zu\n", 
      min_pool_size, size);
  }
  // set the size
  pool->size = size;
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
 * This function is responsible for destroying the tlsf pool structure
 */
void
destroy_tlsf_pool(wtlsf_t *pool) {
  // check for valid input
  if(pool == NULL) {
    printf(" !! Cannot destroy, no valid pool provided\n");
    return;
  }
  printf(" -- Destroying tlsf pool of size %zu\n", pool->size);
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
 * Execute the sequential plan
 */
void
tlsf_bench_seq(wtlsf_t *pool, alloc_plan_t *plan) {
  printf(" !! Running a sequential plan type of size: %zu\n", plan->plan_size);
  int mem_pivot = 0;
  double timed_seg = 0;
  for (int i = 0; i < plan->plan_size; ++i) {
    unsigned long long t = tic(NULL);
    // check if we have an allocation
    if(plan->slot_type[i] == SLOT_MALLOC) {
      //printf(" -- Allocating block at %d with size %zu bytes\n", 
      //  i, plan->block_size[i]);
      plan->mem_ptr[mem_pivot] = tlsf_malloc(pool->tlsf_ptr, plan->block_size[i]);
      plan->cur_malloc_size[mem_pivot] = plan->block_size[i];
      mem_pivot++;
    } else if(plan->slot_type[i] == SLOT_FREE) {
      int free_blk = (int) plan->block_id[i];
      //printf(" -- Freeing block at %d\n", free_blk);
      tlsf_free(pool->tlsf_ptr, plan->mem_ptr[free_blk]);
      //plan->cur_malloc_size[free_blk] = 0;
    } else {
      // error
      printf(" !! Error, encountered empty slot of a full plan\n");
    }
    timed_seg = toc(t, NULL, false);
    // insert the timed segment to array
    plan->timings[i] = timed_seg;
  }
  assert(mem_pivot == plan->plan_size/2);
}

/**
 * Execute the ramp plan
 */
void
tlsf_bench_ramp(wtlsf_t *pool, alloc_plan_t *plan) {
  //TODO
}

/**
 * Execute the hammer plan
 */
void 
tlsf_bench_hammer(wtlsf_t *pool, alloc_plan_t *plan) {
  //TODO
}

/**
 * Execute a scripted custom plan from file import
 */
void
tlsf_bench_custom(wtlsf_t *pool, alloc_plan_t *plan) {
  //TODO
}

/**
 * This function benchmarks tlsf in sequential malloc/frees
 */
void
tlsf_bench(wtlsf_t *pool, alloc_plan_t *plan) {
  // check if pool is null
  if(pool == NULL) {
    printf(" !! Error pool cannot be NULL, cannot continue\n");
    return;
  }
  // check if plan is null
  if(plan == NULL || plan->peak_alloc == 0 || plan->aggregated_alloc == 0) {
    printf(" !! Error allocation plan cannot be NULL, cannot continue\n");
    return;
  }
  // check if our provided pool can execute the plan
  if(plan->peak_alloc > pool->size) {
    printf(" !! Error, pool size of %zu is too small to satisfy peak allocation \
of %zu; cannot continue\n", pool->size/mb_div, plan->peak_alloc/mb_div);
    return;
  }
  // basic info
  printf(" -- Running %zu ops with a pool size of %zu bytes\n", 
    plan->plan_size, pool->size);
  unsigned long long ctx = tic(NULL);
  // execute the sequential plan
  switch(plan->type) {
    case ALLOC_SEQ: {
      tlsf_bench_seq(pool, plan);
      break;
    }
    case ALLOC_RAMP: {
      tlsf_bench_ramp(pool, plan);
      break;
    }
    case ALLOC_HAMMER: {
      tlsf_bench_hammer(pool, plan);
      break;
    }
    case ALLOC_CUSTOM: {
      tlsf_bench_custom(pool, plan);
      break;
    }
    default: {
      tlsf_bench_seq(pool, plan);
      break;
    }
  }
  // find the total elapsed time
  double elapsed = toc(ctx, NULL, false);
  printf(" -- Finished %zu ops in pool, elapsed cycles for bench was %f\n", 
    plan->plan_size, elapsed);
  printf(" -- xput: %lf [malloc/free] ops/cycle\n", plan->plan_size/elapsed);
}

/**
 * little hack to get the numbers showing with two digits even when it's < 10 which
 * is extremely useful to have ISO 8061 compliance when we create the timestamp
 */
void
num_to_str_pad(char *buf, int number) {
  // rudimentary error check...
  if(buf == NULL) {
    return;
  }
  if(number < 10 && number >= 0) {
    snprintf(buf, 3, "0%d", number);
  } else if (number < 99) {
    snprintf(buf, 3, "%d", number);
  }
}

/**
 * Creates a timestamp with the filename based on the ISO 8061 specification.
 */
char *
create_iso8061_ts(char *dir, char *suffix, char* ext) {
  time_t ctime;       // holds unix time
  struct tm *mytime;  // holds local time
  size_t buf_sz = 100;
  // get current time
  time(&ctime);
  // buffers
  char tm_mon_buf[3] = {0};
  char tm_mday_buf[3] = {0};
  char tm_hour_buf[3] = {0};
  char tm_min_buf[3] = {0};
  char tm_sec_buf[3] = {0};
  // get local time
  mytime = localtime(&ctime);
  char *buf = calloc(buf_sz, sizeof(char));
  if(buf == NULL) {
    printf(" !! Error could not allocate memory to create timestamp\n");
    return NULL;
  }

  // perform the conversion
  num_to_str_pad(tm_mon_buf, mytime->tm_mon);
  num_to_str_pad(tm_mday_buf, mytime->tm_mday);
  num_to_str_pad(tm_hour_buf, mytime->tm_hour);
  num_to_str_pad(tm_min_buf, mytime->tm_min);
  num_to_str_pad(tm_sec_buf, mytime->tm_sec);  

  // finally format the timestamp to be in ISO 8061
  snprintf(buf, buf_sz, "%s/%d%s%sT%s%s%sZ_%s.%s", dir,
    mytime->tm_year+1900, tm_mon_buf, tm_mday_buf,
    tm_hour_buf, tm_min_buf, tm_sec_buf, suffix, ext);
  // finally return
  return buf;
}

/**
 * Create the directory
 */
bool
create_dir(char *dir_full_path) {
  struct stat st = {0};
  if(stat(dir_full_path, &st) == -1) {
    printf(" ** Specified dump directory does not exist, creating\n");
    if(mkdir(dir_full_path, 0700) != 0) {
      printf(" !! Could not create directory: %s\n", dir_full_path);
      return false;
    } else {
      printf(" ** Directory (%s) created successfully\n", dir_full_path);
    }
    printf(" ** Directory (%s) already exists\n", dir_full_path);
  }
  return true;
}

/**
 * Create the file to store the results
 */
FILE *
create_dump_file(char *fname) {
  // try to create the directory, if needed
  if(!create_dir("./traces/")) {
    return NULL;
  }
  // create the timestamp
  char *ts_fname = create_iso8061_ts(dump_dir, fname, dump_ext);
  if(ts_fname == NULL) {
    printf(" !! Error, null timestamp returned\n");
    return NULL;
  }
  // open the file
  FILE *fp = fopen(ts_fname, "w");
  // check for errors
  if(fp == NULL) {
    printf(" !! Error, could not open the file\n");
  } else {
    printf(" ** Trace dump file %s open for writing\n", ts_fname);
  }
  // now free up the timestamp resources
  if(ts_fname != NULL) {
    free(ts_fname);
  }
  // return the file for writing
  return fp;
}

/**
 * Function that notified the user for trace dump file closure.
 */
void
close_dump_file(FILE *fp) {
  printf(" ** Trace dump file closed successfully\n");
  // close the file
  fclose(fp);
}

/**
 * Dump results into a plot-friendly format
 */
void
dump_plan(alloc_plan_t *plan, char *fname) {
  if(plan == NULL) {
    printf(" !! Error null plan supplied, cannot continue\n");
  } else {
    printf(" ** Dumping allocation plan details with size %zu\n", plan->plan_size);
  }
  FILE *fp = create_dump_file(fname);
  // sanity check
  if(fp == NULL) {
    printf(" !! Error, null file pointer, cannot continue dump\n");
    return;
  }
  // write the plan size in the first line
  fprintf(fp, "%zu\n", plan->plan_size);
  // now write the header
  fprintf(fp, "op_type,chunk_size,block_id,exec_time\n");
  // loop and dump
  size_t chunk_size = 0;
  size_t free_chunk_size = 0;
  size_t blk_cnt = 0;
  double exec_time = 0;
  size_t block_id = 0;
  for (int i = 0; i < plan->plan_size; ++i) {
    char *op_type = NULL;
    chunk_size = plan->block_size[i];
    exec_time = plan->timings[i];
    block_id = plan->block_id[i];
    // differentiate allocation based on alloc/dealloc op
    if(plan->slot_type[i] == SLOT_FREE) {
      op_type = "free";
      free_chunk_size = plan->cur_malloc_size[block_id];
      fprintf(fp, "%s,%zu,%zu,%lf\n", op_type, free_chunk_size, block_id, exec_time);
    } else if(plan->slot_type[i] == SLOT_MALLOC) {
      op_type = "malloc";
      fprintf(fp, "%s,%zu,%zu,%lf\n", op_type, chunk_size, blk_cnt, exec_time);
      // sanity check
      assert(block_id == blk_cnt);
      // increment the block count
      blk_cnt++;
    } else {
      printf(" !! Error encountered empty slot on a full plan\n");
    }
  }
  // close the file
  close_dump_file(fp);
}

/** 
 * function to check header
 */
bool
check_trace_header(char *header) {
  char *tok = NULL;
  // op_type token
  tok = strtok(header, tok_delim_cm);
  if(tok == NULL || strcmp(tok, "op_type") != 0) {
    printf(" !! Header seems invalid, first token needs to be 'op_type' cannot continue\n");
    return false;
  }
  // chunk_size token
  tok = strtok(NULL, tok_delim_cm); 
  if(tok == NULL || strcmp(tok, "chunk_size") != 0) {
    printf(" !! Header seems invalid, second token needs to be 'chunk_size' cannot continue\n");
    return false;
  } 
  // block_id token
  tok = strtok(NULL, tok_delim_nl);
  if(tok == NULL) {
    printf(" !! Header seems invalid, third token needs to be 'block_id' cannot continue\n");
    return false;
  } else if(strcmp(tok, "block_id") == 0) {
    printf(" -- Header seems valid, trying to parse plan\n");
    return true;
  } else if(strcmp(tok, "block_id,exec_time") == 0) {
    printf(" !! Header seems valid, but seems to be from output trace; using first 3 fields\n");
    parsing_out_traces = true;
    return true;
  } else {
    printf(" !! Header seems invalid, cannot continue\n");
    return false;
  }
}

/**
 * Function that parses each line for the given allocation plan
 *
 * This function expects each of the lines to be of the following
 * format:
 * 
 * op_type,chunk_size,block_id(,exec_time)
 *
 * Currently op_type, chunk_size, and block_id are mandatory. The field 
 * exec_time optional and is present so that we are able to load synthetic 
 * traces output which have an additional field for execution timing.
 *
 * Specification:
 *
 *  op_type: has to be either 'malloc' or 'free'
 *  chunk_size: for malloc has to be the block size
 *  block_id: the unique block id, there are as many block id's as many 
 *            allocations and the total amount of id's must be half of 
 *            the plan size
 *  (timings): the optional field which has timing measurements if it's an
 *             already executed plan. This field is *ignored*.
 */
bool
parse_trace_line(char *line, int *line_no, int *malloc_cnt, 
  size_t *cur_alloc, alloc_plan_t *plan) {
  char *tok = NULL;
  size_t tok_cnt = 1;
  slot_type_t slot = SLOT_EMPTY;
  
  // handle op_type
  tok = strtok(line, tok_delim_cm);
  if(tok == NULL) {
    printf(" !! Error, encountered at line %d, NULL token at position %zu\n",
      (*line_no) + 1, tok_cnt);
    return false;
  } else if(strcmp("malloc", tok) == 0) {
    printf(" -- malloc op_type detected\n");
    slot = SLOT_MALLOC;
  } else if(strcmp("free", tok) == 0) {
    printf(" -- free op_type detected\n");
    // handle free op_type
    slot = SLOT_FREE;
  } else {
    printf(" !! Error invalid op_type detected: '%s', expecting \
either 'malloc' or 'free'\n", tok);
    return false;
  }

  // increment token count
  tok_cnt++;

  // handle chunk_size
  tok = strtok(NULL, tok_delim_cm);
  long chunk_size = 0;
  if(tok == NULL) {
    printf(" !! Error, encountered at line %d, NULL token at position %zu\n",
      *line_no, tok_cnt);
    return false;
  }

  errno = 0;
  chunk_size = strtol(tok, NULL, 10);
  if(chunk_size <= 0 || chunk_size == ULLONG_MAX || errno == EINVAL) {
    printf(" !! Error, encountered at line %d, could not convert \
token '%s' at position %zu to 'size_t'\n", (*line_no) + 1, tok, tok_cnt);
    return false;
  } else {
    printf(" -- chunk_size of %ld bytes, parsed\n", chunk_size);
  }

  // increment token count
  tok_cnt++;

  // handle block_id
  tok = strtok(NULL, tok_delim_nl);
  if(tok == NULL) {
    printf(" !! Error, encountered at line %d, NULL token at position %zu\n",
      (*line_no) + 1, tok_cnt);
    return false;
  }
  // check if we have four fields, in the case of out traces
  if(parsing_out_traces) {
    tok = strtok(tok, tok_delim_cm);
    if(tok == NULL) {
      printf(" !! Error, encountered at line %d, NULL token at position %zu\n",
        (*line_no) + 1, tok_cnt);
      return false;
    }
  }

  // parse the block
  long block_id = 0;
  errno = 0;
  block_id = strtol(tok, NULL, 10);
  if(block_id < 0 || ((block_id == 0 || block_id == ULONG_MAX) && errno == EINVAL)) {
    printf(" !! Error, encountered at line %d, could not convert \
token '%s' at position %zu to 'size_t'\n", (*line_no) + 1, tok, tok_cnt);
    return false;
  } else if(block_id > plan->plan_size/2) {
    printf(" !! Error, it appears block_id: %ld is larger than the \
allowed limit plan_size/2 (%zu)\n", block_id, plan->plan_size/2);
      return false;
  } else {
    printf(" -- block_id %ld, parsed\n", block_id);
  }
  
  // calculate the index
  int cur_idx = (*line_no)-line_offset;
  plan->block_id[cur_idx] = block_id;
  if(plan->slot_type[cur_idx] != SLOT_EMPTY) {
    printf(" !! Error, encountered at line %d a non-empty slot in an unexpected \
position.\n", (*line_no) + 1);
    return false;
  } else if(slot == SLOT_MALLOC) {
    if(block_id != *malloc_cnt) {
      printf(" !! Error, encountered at line %d block_id (%ld) provided for \
malloc is not valid, expecting: %d\n", (*line_no) + 1, block_id, *malloc_cnt);
      return false;
    }
    plan->slot_type[cur_idx] = SLOT_MALLOC;       // set the slot type
    plan->malloc_tag_time[*malloc_cnt] = cur_idx; // set the malloc tagged time
    // increment the malloc counter
    (*malloc_cnt)++;
    // update the current allocation size
    *cur_alloc += chunk_size;
    // add the aggregated allocation size
    plan->aggregated_alloc += chunk_size;
  } else if(slot == SLOT_FREE) {
    // grab the corresponding malloc slot index
    int malloc_slot = plan->malloc_tag_time[block_id];
    // now check if the pointed 'malloc' has the same block id as the 'free' being parsed
    if(plan->block_id[malloc_slot] != block_id) {
      printf(" !! Error, encountered at line %d block_id for respective \
malloc (%zu)/free (%zu) do not match.\n", 
      (*line_no) + 1, plan->block_id[malloc_slot], block_id);
      return false;
    }
    plan->slot_type[cur_idx] = SLOT_FREE;         // set the slot type
    // update the current allocation size
    *cur_alloc -= chunk_size;
  } else {
    printf(" !! Error, encountered at line %d an unexpected empty-slot type.\n", 
      (*line_no) + 1);
    return false;
  }

  // check if we need to adjust out peak allocation size
  if(plan->peak_alloc < *cur_alloc) {
    plan->peak_alloc = *cur_alloc;
  }

  // after all the checks, finally return.
  return true;
}

/**
 * Parse the plan size
 */
bool
parse_plan_size(char *line, alloc_plan_t *plan) {
  long ret;
  errno = 0;
  ret = strtol(line, NULL, 10);
  if(ret <= 0 || ret == ULONG_MAX || errno == EINVAL) {
    printf(" !! Error, could not parse the plan size number in the first line\n");
    return false;
  } else {
    printf(" !! Parsed plan size of %zu\n", ret);
    plan->plan_size = (size_t) ret;
    // preallocate the plan
    return perform_plan_prealloc(plan);
  }
}

/** 
 * Load allocation plan from trace file
 */
alloc_plan_t *
import_alloc_plan(char *fname, alloc_plan_t *plan) {
  // check if filename is null
  if(fname == NULL) {
    printf(" !! Error, cannot have null filename, cannot continue\n");
    return NULL;
  }
  // check if plan is null
  if(plan == NULL) {
    printf(" !! Error, cannot have a null allocation plan, cannot continue\n");
    return NULL;
  }
  // open the file
  FILE *fp = fopen(fname, "r");
  if(fp == NULL) {
    printf(" !! Error, failed to open the file at: %s for reading\n", fname);
    return NULL;
  }
  plan->peak_alloc = 0;
  plan->aggregated_alloc = 0;
  // initialize file parsing variables
  char *fline = NULL;
  size_t llen = 0;
  size_t bytes_read = 0;
  size_t cur_alloc = 0;
  int lcnt = 0;
  int malloc_cnt = 0;
  bool ret = false;
  // loop through the file
  while((bytes_read = getline(&fline, &llen, fp)) != -1) {
    printf(" -- Parsing line %d of %zu bytes\n", lcnt+1, bytes_read);
    // checks for parse type
    if(lcnt == 0) {
      // parse first line, which preallocates the plan
      ret = parse_plan_size(fline, plan);
    } else if(lcnt == 1) {
      // parse the csv header
      ret = check_trace_header(fline);
    } else {
      // parse the actual traces
      ret = parse_trace_line(fline, &lcnt, &malloc_cnt, &cur_alloc, plan);
    }
    // check for possible errors
    if(!ret) {
      printf(" !! Fatal parse error encountered at line %d, aborting\n", lcnt+1);
      break;
    }
    // increment the line counter
    lcnt++;
  }

  // perform the final checks
  if(lcnt-line_offset != plan->plan_size) {
    printf(" !! Error, it appears that import file ops (%d) are \
more than the parsed plan size (%zu)\n", lcnt-line_offset, plan->plan_size);
    ret = false;
  } else if(malloc_cnt != plan->plan_size / 2) {
    printf(" !! Error, it appears that malloc counts (%d) is not \
  equal to half plan size (%zu) \n", malloc_cnt, plan->plan_size / 2);
    ret = false;
  } 

  // if all went well, set the plan type to be custom
  plan->type = ALLOC_CUSTOM;

  // clean up the line
  if(fline) {
    free(fline);
  }
  // check what to return
  if(!ret) {
    destroy_alloc_plan(plan);
    return NULL;
  } else {
    return plan;
  }
}

/**
 * Print allocation plan details (mostly for debug reasons)
 */
void
print_plan(alloc_plan_t *plan) {  
  size_t chunk_size = 0;
  size_t free_chunk_size = 0;
  size_t blk_cnt = 0;
  size_t block_id = 0;
  double exec_time = 0;
  printf(" ** Printing allocation plan details (size %zu)\n", plan->plan_size);
  for (int i = 0; i < plan->plan_size; ++i) {
    char *op_type = NULL;
    chunk_size = plan->block_size[i];
    exec_time = plan->timings[i];
    //printf("\t%s, %zu, %lf, %zu (%d)\n", op_type, chunk_size, exec_time, idx, i);
    // differentiate allocation based on alloc/dealloc op
    if(plan->slot_type[i] == SLOT_FREE) {
      op_type = "free";
      free_chunk_size = plan->cur_malloc_size[block_id];
      printf("\t%s, %zu, %lf, %zu (%d)\n", op_type, free_chunk_size, exec_time, block_id, i);
    } else if(plan->slot_type[i] == SLOT_MALLOC) {
      op_type = "malloc";
      printf("\t%s, %zu, %lf, %zu (%d)\n", op_type, chunk_size, exec_time, block_id, i);
      // sanity check
      assert(blk_cnt == block_id);
      // increment block
      blk_cnt++;
    } else {
      printf(" !! Error, encountered empty slot on a full plan");
    }
  }
  printf(" ** End of allocation plan details print\n");
}

/**
 * Main stub
 */
int
main(int argc, char **argv) {
  printf("\n");
  // initialize the random number generator
  srand(2);
  // parse arguments
  parse_args(argc, argv);

  // our pool structure
  wtlsf_t pool = {0};
  // our plan structure
  alloc_plan_t plan = {0};

  
  // check for successful allocation plan generation
  if(!gen_alloc_plan(bench_trials, &plan)) {
    return 0;
  } 
  
  //print_plan(&plan);
  
  // now run the experiment
  char *tag = "Global Pool";
  unsigned long long c_ctx = tic(tag);

  // create the pool
  create_tlsf_pool(&pool, pool_size);
  // now run the bench
  tlsf_bench(&pool, &plan);
  // destroy the pool
  destroy_tlsf_pool(&pool);
  
  toc(c_ctx, tag, true);
  
  //print_plan(&plan);

  // dump the plan
  dump_plan(&plan, dump_mem_trace_suffix);
  
  // finally destroy the allocation plan
  destroy_alloc_plan(&plan);
  
  printf("\n");
  return 0;
}
