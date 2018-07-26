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
#include <sys/types.h>
#include <sys/stat.h>

/* tlsf lib */
#include "tlsf.h"

/* tlsf wrapper for convenience */
typedef struct _wtlsf_t  {
  tlsf_t *tlsf_ptr;
  char *mem;
  size_t size;
} wtlsf_t;

/* allocation plan type */
typedef enum _alloc_plan_type_t {
  ALLOC_SEQ = 0,
  ALLOC_RAMP = 1,
  ALLOC_HAMMER = 2,
  /* maybe more to be added? */
} alloc_plan_type_t;

/* allocation plan helper */
typedef struct _alloc_plan_t {
  // essentials
  char **mem_ptr;           // memory block pointers
  size_t *cur_malloc_size;  // current malloc size
  size_t *block_size;       // memory block actual size
  bool *is_alloc;           // flag to show if it's for malloc or free
  // statistics
  size_t peak_alloc;        // peak allocation for this plan 
  size_t aggregated_alloc;  // aggregated allocation for this plan
  // timing stuff
  double *timings;          // timings for each op performed in the plan
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

// trace dump related
char *dump_dir = "./traces";  // dump directory
char *dump_ext = "csv";       // dump extension

/**
 * Functions
 */

/**
 * tic: function that starts the timing context
 */
clock_t
tic(char *msg) {
  if(msg != NULL) {
    printf(" ** Tick (%s)\n", msg);
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
      plan->block_size[j] = blk_size;
      plan->is_alloc[j] = true; 
      // now tag deallocation blocks
      plan->block_size[j+trail_size] = mem_alloc; // index in mem array instead of size
      plan->is_alloc[j+trail_size] = false;
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
      break;
    }
    case ALLOC_HAMMER: {
      printf(" ** Tagging blocks with allocation plan: HAMMER\n");
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
generate_alloc_plan(size_t plan_size, alloc_plan_t *plan) {
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

  // first of all, allocate required vars
  
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
  // is_alloc array
  if((plan->is_alloc = calloc(plan_size, sizeof(bool))) == NULL) {
    printf(" !! Failed to allocate is_alloc array\n");
    free(plan->block_size);
    free(plan->timings);
    return false;
  }
  // cur_malloc_size array
  if((plan->cur_malloc_size = calloc(plan_size, sizeof(size_t))) == NULL) {
    printf(" !! Failed to allocate cur malloc size array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->is_alloc);
    return false;
  }
  // mem_ptr
  if((plan->mem_ptr = calloc(plan_size / 2, sizeof(char *))) == NULL) {
    printf(" !! Failed to allocate pointer array\n");
    free(plan->block_size);
    free(plan->timings);
    free(plan->is_alloc);
    free(plan->cur_malloc_size);
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
  }
  // check if we have a cur malloc size type array
  if (plan->cur_malloc_size != NULL) {
    printf(" -- Valid cur malloc size array found, freeing\n");
    free(plan->cur_malloc_size);
  }
  // check if we have a valid block size type array
  if(plan->block_size != NULL) {
    printf(" -- Valid block size array found, freeing\n");
    free(plan->block_size);
  }
  // check if we have a valid allocation type array
  if(plan->is_alloc != NULL) {
    printf(" -- Valid allocation type array found, freeing\n");
    free(plan->is_alloc);
  }
  // check if we have a valid timings array
  if(plan->timings != NULL) {
    printf(" -- Valid timings array found, freeing\n");
    free(plan->timings);
  }
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
    clock_t t = tic(NULL);
    // check if we have an allocation
    if(plan->is_alloc[i]) {
      //printf(" -- Allocating block at %d with size %zu bytes\n", 
      //  i, plan->block_size[i]);
      plan->mem_ptr[mem_pivot] = tlsf_malloc(pool->tlsf_ptr, plan->block_size[i]);
      plan->cur_malloc_size[mem_pivot] = plan->block_size[i];
      mem_pivot++;
    } else {
      int free_blk = (int) plan->block_size[i];
      //printf(" -- Freeing block at %d\n", free_blk);
      tlsf_free(pool->tlsf_ptr, plan->mem_ptr[free_blk]);
      //plan->cur_malloc_size[free_blk] = 0;
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
 * This function benchmarks tlsf in sequential malloc/frees
 */
void
tlsf_bench(wtlsf_t *pool, alloc_plan_t *plan) {
  // check if pool is null
  if(pool == NULL) {
    printf(" !! Error pool cannot be NULL, cannot continue\n");
  }
  // check if plan is null
  if(plan == NULL) {
    printf(" !! Error allocation plan cannot be NULL, cannot continue\n");
  }
  // basic info
  printf(" -- Running %zu ops with a pool size of %zu bytes\n", 
    plan->plan_size, pool->size);
  clock_t ctx = tic(NULL);
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
    default: {
      tlsf_bench_seq(pool, plan);
      break;
    }
  }
  // find the total elapsed time
  double elapsed = toc(ctx, NULL, false);
  printf(" -- Finished %zu ops in pool, elapsed time for bench was %f seconds\n", 
    plan->plan_size, elapsed);
  printf(" -- xput: %lf [malloc/free] ops/sec\n", plan->plan_size/elapsed);
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
  if(number < 10) {
    snprintf(buf, 3, "0%d", number);
  } else if (number < 99) {
    snprintf(buf, 3, "%d", number);
  }
}

/**
 * Creates a timestamp with the filename
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

  fprintf(fp, "op_type,chunk_size,exec_time,block\n");
  // loop and dump
  size_t chunk_size = 0;
  size_t free_chunk_size = 0;
  size_t blk_cnt = 0;
  double exec_time = 0;
  for (int i = 0; i < plan->plan_size; ++i) {
    char *op_type = plan->is_alloc[i] == true ? "malloc" : "free";
    chunk_size = plan->block_size[i];
    exec_time = plan->timings[i];
    // differentiate allocation based on alloc/dealloc op
    if(plan->is_alloc[i] == false) {
      free_chunk_size = plan->cur_malloc_size[chunk_size];
      fprintf(fp, "%s,%zu,%lf,%zu\n", op_type, free_chunk_size, exec_time, chunk_size);
    } else {
      fprintf(fp, "%s,%zu,%lf,%zu\n", op_type, chunk_size, exec_time, blk_cnt);
      blk_cnt++;
    }
  }
  // close the file
  close_dump_file(fp);
}

/** 
 * Load allocation plan from trace file
 */
alloc_plan_t *
import_plan(char *fname, alloc_plan_t *plan) {
  if(fname == NULL) {
    printf(" !! Error, cannot have null filename, cannot continue\n");
    return NULL;
  }

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
  // todo
  return NULL;
}

/**
 * Print allocation plan details (mostly for debug reasons)
 */
void
print_plan(alloc_plan_t *plan) {  
  size_t chunk_size = 0;
  size_t free_chunk_size = 0;
  size_t blk_cnt = 0;
  double exec_time = 0;
  printf(" ** Printing allocation plan details (size %zu)\n", plan->plan_size);
  for (int i = 0; i < plan->plan_size; ++i) {
    char *op_type = plan->is_alloc[i] == true ? "malloc" : "free";
    chunk_size = plan->block_size[i];
    exec_time = plan->timings[i];
    //printf("\t%s, %zu, %lf, %zu (%d)\n", op_type, chunk_size, exec_time, idx, i);
    // differentiate allocation based on alloc/dealloc op
    if(plan->is_alloc[i] == false) {
      free_chunk_size = plan->cur_malloc_size[chunk_size];
      printf("\t%s, %zu, %lf, %zu (%d)\n", op_type, free_chunk_size, exec_time, chunk_size, i);
    } else {
      printf("\t%s, %zu, %lf, %zu (%d)\n", op_type, chunk_size, exec_time, blk_cnt, i);
      blk_cnt++;
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
  if(!generate_alloc_plan(bench_trials, &plan)) {
    return 0;
  } 

  //print_plan(&plan);

  // now run the experiment
  char *tag = "Global Pool";
  clock_t c_ctx = tic(tag);

  // create the pool
  create_tlsf_pool(&pool, pool_size);
  // now run the bench
  tlsf_bench(&pool, &plan);
  // destroy the pool
  destroy_tlsf_pool(&pool);
  
  toc(c_ctx, tag, true);

  //print_plan(&plan);

  // dump the plan
  //dump_plan(&plan, "mem_trace");

  // finally destroy the allocation plan
  destroy_alloc_plan(&plan);
 
  
  printf("\n");
  return 0;
}
