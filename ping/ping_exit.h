#ifndef IPUTILS_PING_EXIT_H
#define IPUTILS_PING_EXIT_H

#ifndef IPUTILS_PING_H
#error ping_exit.h is not to be included directly, but via ping.h
#endif

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>

#include "iputils_common.h"

struct ping_rts; // Forward definition

#define DEBUG_EXIT_COND 0
#define DEBUG_MAP_EXTENSION 0

#if DEBUG_EXIT_COND < 5
// DEBUG_MAP_EXTENSION only meaningful with debuglevel at minimum 5
#undef DEBUG_MAP_EXTENSION
#define DEBUG_MAP_EXTENSION 0
#endif

#define EXIT_COUNTER_TYPE long // compatible with struct ping_rts counters

// flags definitions
#define EXIT_COUNT_FAILED 1   // Count failed pings (default=success)
#define EXIT_SEQUENCE     2   // Require count in sequence
#define EXIT_COND_STATUS    4 // Request exit status code to be status of exit condition
#define EXIT_REPORT_SUCCESS 8 // Print out only success count  \_ Both otgether print out <success>/<fail>
#define EXIT_REPORT_FAILURES 16 // print out only falure count /
#define EXIT_REPORT_MAP   32  // Report ping map
#define EXIT_REPORT_SILENT 64 // Report only the exit status information (if any report requested!)
#define EXIT_REPORT_STATE 128 // Report condition state at exit 'T'/'F'

#define EXIT_COND_LOC_FLAGS EXIT_SEQUENCE

#define EXIT_REPORT_FLAGS (EXIT_REPORT_SUCCESS | EXIT_REPORT_FAILURES | EXIT_REPORT_MAP | EXIT_REPORT_STATE)

#define EXIT_DEFAULT_SUCCESS_MAP '+'
#define EXIT_DEFAULT_FAILURE_MAP '-'

#define EXIT_COND_OPTIONS "xnNmqc"

#if DEBUG_MAP_EXTENSION
#define EXIT_COND_DEFAULT_MAP_SIZE 20
#define EXIT_COND_MAX_INITIAL_MAP_SIZE 10
#define EXIT_COND_MAP_EXTENSION 3
#else
#define EXIT_COND_DEFAULT_MAP_SIZE 100
#define EXIT_COND_MAX_INITIAL_MAP_SIZE 512
#define EXIT_COND_MAP_EXTENSION 512
#endif

#define EXIT_COND_MAP_CHAR_FAILED_IDX 0
#define EXIT_COND_MAP_CHAR_SUCCESS_IDX 1

struct exit_condition
{
  EXIT_COUNTER_TYPE expect;   // expected count
  EXIT_COUNTER_TYPE ntransmitted; // Currently known sequence state
  EXIT_COUNTER_TYPE nreceived;
  EXIT_COUNTER_TYPE sequence;
  int condition_met;
  char map_chars[2];          // [0]=failure [1]=sucess
  size_t map_size;
  size_t max_map_size;
  char *ping_map;
  size_t mapper_pos;
  uint16_t flags;
};

#define INIT_EXIT_CONDITION(ptr) \
    (ptr)->expect = 0;\
    (ptr)->ntransmitted = 0;\
    (ptr)->nreceived = 0;\
    (ptr)->sequence = 0;\
    (ptr)->condition_met = 0;\
    (ptr)->map_chars[0] = EXIT_DEFAULT_FAILURE_MAP; (ptr)->map_chars[1] = EXIT_DEFAULT_SUCCESS_MAP;\
    (ptr)->map_size=0;\
    (ptr)->mapper_pos=0;\
    (ptr)->max_map_size=EXIT_COND_DEFAULT_MAP_SIZE;\
    (ptr)->ping_map = NULL;\
    (ptr)->flags = 0;

extern struct exit_condition *parse_exit_cond(char *opt);
extern int check_exit_condition(struct ping_rts *rts);
extern void exit_cond_last_gasp(int status, void *arg);
extern void print_exit_cond_report(struct ping_rts *rts, int force);
extern int exit_cond_status_update(int status);

#endif /* IPUTILS_PING_H */
