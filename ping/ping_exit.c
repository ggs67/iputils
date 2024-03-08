/*
 *                      P I N G _ E X I T . C
 *
 * Implementing -x option defining exit conditions
 *
 * Author -
 *      Gaston Gloesener
 *      March 2024
 *
 * Status -
 *      Public Domain.  Distribution Unlimited.
 * Bugs -
 *
 */

/*
syntax: -x <exit-cond>

<exit-cond> : <count><loc>[:<opt>]
<count>     : <n>   - exit after receiving <n> (integer > 0) successful  ECHO_RESPONSEs
              -<n>  - exit after <n> unsuccessful pings
<loc>       : if not specified, exit whenever <n> (un)successful pings have been seen, as requested
              s - only exit if <count> was seen in sequence
<opt>       : <opt>* (options can be combined)
              x     - ping exit code reflects if exit-condition was met, rather than normal ping status. Unsuccessful status
                      could be seen if -c or -w are reached before the condition is met
              c     - report exit condition status 'T' (true, eg. met) or 'F' (false, eg. not met at exit)
              [-+]n - ping is silent only outputting a single integer '<m>' at exit, specifying the number of non matching
                      pings (by default). This is most useful with the 'x' option, so on success exit code we know that
                      the condition was met and thus <n> (un)successful pings where seen. This option outputs the number of
                      pings of opposite status than specified in the condition that have been seen. '-' (unsuccessful) or
                      '+' (successfull) may be specified to explicitly request the given count to be output instead

                      Example: -x 3:xn exits with status 0 (condition met (x option)) and prints out 1, this means that we
                               did see 3 successful pings as requested and 1 unsuccessful

                      Example: Waiting for a host to come online I may use -x 10s:x+n (or N instead of +n), if <success> is
                               greater than 10 I know that there have been a few failures after the host was first seen
                               (failures are expected as long as host is down)

              N     - ping is silent only outputting counts <success>/<failures>

              m(<n>[:<s><f>]) - ping is silent only outputting the ping map. Ping map is a sequence of characters, one per
                                ping which show if the corresponding ping was successful (+) or unsuccessful (-)
                                Parenthesis are only needed if options are passed:
                                  <n>   : only the <n> last pings are reported (default=all)
                                  <s><f>: two characters (optional) overriding successful <s> and unsuccessful <f> ping
                                          characters in the map
              q     - exit condition quisence all output from ping also the output generated after the -q ping option
                      This flag only has effect if any reporting has been reuestes (n,N or m options)
              NOTES:  - n and N are mutually exclusive
                      - 'n' or 'N' can be combined with 'm' and/or 'c', for any combination the order is always c/n1/n2/m
                        where n2 is only output if 'N' was used and then n1=sucess, n2=failures, with 'n' only one count is
                        output as reuested (see 'n' option). Any missing option will also not be output including its separator.
                      - -x can of course be combined with any other ping option, for instance combinations with -c allow
                        for some additional explicit conditions not having to be covered via -x. Like "-c 10 -x 10:x"
                        making sure that we had 10 only successful pings, or if we want reverse status for same condition
                        "-c 10 -x -1:x" (status=0 if ever we see an unsuccessful ping)
                      - the default map size is EXIT_COND_DEFAULT_MAP_SIZE (100)
                      - for any map size requested, the max8imum initial size will be EXIT_COND_MAX_INITIAL_MAP_SIZE (512)
                        extending each time ba EXIT_COND_MAP_EXTENSION (512) without exceeding the requested maximum
                      - when the map is full it wraps around overwriting the oldest entries
                      - if the 'q' option is used only the report is output on a single line, if not, it is additional
                        labeled by some text to be able to localize and parse it
 */
#include "ping.h"

#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#define DEFAULT_MSG_BUFLEN 256

#ifndef FALSE
#define FALSE 0
#define TRUE (!FALSE)
#endif

int global_exit_cond_status = -1; // Disable exit status by default

static inline int get_exit_cond_success(struct exit_condition *cond) { return cond->nreceived; }
static inline int get_exit_cond_failures(struct exit_condition *cond) { return cond->ntransmitted - cond->nreceived; }


#if DEBUG_EXIT_COND > 0
#define debug(level, msg, ...) _debug(level, msg, __VA_ARGS__)
#define debugl(level, msg) _debug(level, msg)
static void _debug(int level, char *msg, ...)
{
    if(level > DEBUG_EXIT_COND) return;

    va_list ap;

    va_start(ap, msg);
    fputs("DEBUG: ", stderr);
    vfprintf(stderr, msg, ap);
    putc('\n', stderr);
    va_end(ap);
}
#else
#define debug(n, msg, ...)
#define debugl(n, msg)
#endif

static void extend_ping_map(struct exit_condition *cond)
{
    // NOTE: we always allocate one additional character to allow
    //       for fast null-terminated string printing
    if(cond->ping_map == NULL)
    {
        cond->map_size = (cond->max_map_size <= EXIT_COND_MAX_INITIAL_MAP_SIZE) ? cond->max_map_size : EXIT_COND_MAX_INITIAL_MAP_SIZE;

        debug(5,"DEBUG: creating ping_map with %ld+1 bytes", cond->map_size);
        debug(5,"       max_map_size=%ld, EXIT_COND_MAX_INITIAL_MAP_SIZE=%d", cond->max_map_size, EXIT_COND_MAX_INITIAL_MAP_SIZE);

        cond->ping_map = malloc(cond->map_size+1);
        cond->mapper_pos = 0;
    }
    else
    {
        if(cond -> map_size >= cond->max_map_size) return;
#if DEBUG_EXIT_COND >= 5
        size_t old_mapsize = cond->map_size;
#endif
        cond->map_size += EXIT_COND_MAP_EXTENSION;
        if(cond->map_size > cond->max_map_size) cond->map_size = cond->max_map_size;
#if DEBUG_EXIT_COND >= 5
        debug(5, "DEBUG: extending ping_map from %ld+1 by %d to %ld+1 bytes", old_mapsize, EXIT_COND_MAP_EXTENSION, old_mapsize + EXIT_COND_MAP_EXTENSION);
        if(cond->map_size != old_mapsize + EXIT_COND_MAP_EXTENSION)
            debug(5, "       actual size was reduced to %ld+1 due to limitation by max_map_size=%ld", cond->map_size, cond->max_map_size);
        debug(5, "       max_map_size=%ld, EXIT_COND_MAX_INITIAL_MAP_SIZE=%ld", cond->max_map_size, EXIT_COND_MAX_INITIAL_MAP_SIZE);
#endif
        cond->ping_map = realloc(cond->ping_map, cond->map_size+1);
    }

    // We can only come here if the map was extended (including initial creation)
    cond->ping_map[cond->map_size-1] = '\0'; // Marker to recognize ring warps
}

static void map_ping(struct exit_condition *cond, int status)
{
    char c = cond->map_chars[status ? EXIT_COND_MAP_CHAR_SUCCESS_IDX : EXIT_COND_MAP_CHAR_FAILED_IDX];
    size_t pos = cond->mapper_pos;

    if(pos==cond->map_size)
    {
        extend_ping_map(cond);
        if(pos == cond->map_size) pos=0;
    }

    cond->ping_map[pos++] = c;
    cond->mapper_pos = pos;

    assert(pos <= cond->map_size);
}

static int _check_exit_condition(struct ping_rts *rts)
{
struct exit_condition *cond = rts->opt_exit_cond;
int status;

    if(cond == NULL) return FALSE;

    debug(9, "check_exit_condition: ntrans: %ld nrec: %ld nerror: %ld", rts->ntransmitted, rts->nreceived, rts->nerrors);

    EXIT_COUNTER_TYPE nsuccess= rts->nreceived - cond->nreceived;
    EXIT_COUNTER_TYPE nfailure= rts->ntransmitted - cond->ntransmitted - nsuccess;
    //We expect only advancement of 1, either success or failure
    if(nsuccess == 0 && nfailure == 0) return FALSE; // No counter increase

    assert( !(nsuccess == 1) != !(nfailure ==1) ); // XOR
    cond->ntransmitted = rts->ntransmitted;
    cond->nreceived = rts->nreceived;
    status=(nsuccess > 0);

    if(cond->flags & EXIT_REPORT_MAP) map_ping(cond, status);

    if(cond->flags & EXIT_SEQUENCE )
    {
        if(cond->flags & EXIT_COUNT_FAILED)
        {
            if(status)
                cond->sequence = 0;
            else
                cond->sequence++;
        }
        else
        {
            if(status)
                cond->sequence++;
            else
                cond->sequence = 0;
        }
        return (cond->sequence == cond->expect);
    }
    else
    {
        if(cond->flags & EXIT_COUNT_FAILED)
        {
            return ((cond->ntransmitted - cond->nreceived)  >= cond->expect);
        }
        else
        {
            return (cond->nreceived >= cond->expect);
        }
    }
}

static void print_ping_map(struct exit_condition *cond, int dbg)
{
    int map_size = cond->map_size;
    int mapper_pos = cond->mapper_pos;

    // We need to check if mapper_pos is start or end marker
    if(cond->ping_map[map_size-1] && mapper_pos < map_size)
    {   // If the last slot was used and mapper_pos not end of map
        // we are in wrapped condition where mapper_pos is the start
        // of the map
        // Print first part (end of map-ring)...
        cond->ping_map[cond->map_size]='\0';
        fputs(cond->ping_map + mapper_pos, stdout);

        ///...and second part
        char keep = cond->ping_map[mapper_pos];
        cond->ping_map[mapper_pos] ='\0';
        fputs(cond->ping_map, stdout);

        // Usually this function is called at exit and restore of the map character would be unecessary,
        // but it is neglectable little overhead avoid future bugs in th efirst place, and also allowing
        // to use this function for debugging
        cond->ping_map[mapper_pos] =keep;
    }
    else
    { // ...otherwise mapper_pos is end marker
        cond->ping_map[mapper_pos]='\0'; // Terminate map
        fputs(cond->ping_map, stdout);
        // No need to reset appended \0 asthis is not yet part of the map
    }

    if(dbg)
    {
        putchar('\n');
        for(int i=0; i < mapper_pos; ++i) putchar(' ');
        putchar('^');
        putchar('\n'); // In debug mode we also end with EOL
    }
}

static void report(struct exit_condition *cond, int first, int display, char *fmt, ...)
{
static int _app = FALSE;
va_list ap;

    if(first) _app=FALSE;
    if(!display) return;

    va_start(ap, fmt);
    if(_app)
        putchar('/');
    else
        _app = TRUE;

    if(fmt==NULL)
        print_ping_map(cond, FALSE);
    else
        vprintf(fmt, ap);
    va_end(ap);
}

void print_exit_cond_report(struct ping_rts *rts, int force)
{
struct exit_condition *cond = rts->opt_exit_cond;

    if(cond == NULL) return;
    if(!force && ((cond->flags & EXIT_REPORT_FLAGS) == 0)) return;

    // If silent mode not selected, lable report
    if((cond->flags && EXIT_REPORT_SILENT)==0) printf("exit condition report:");

    //print_ping_map(rts->opt_exit_cond, TRUE);
    report(cond,TRUE , cond->flags & EXIT_REPORT_STATE, "%c", (cond->condition_met ? 'T' : 'F') );
    report(cond,FALSE, cond->flags & EXIT_REPORT_SUCCESS, "%d", get_exit_cond_success(rts->opt_exit_cond));
    report(cond,FALSE, cond->flags & EXIT_REPORT_FAILURES, "%d", get_exit_cond_failures(rts->opt_exit_cond));
    report(cond,FALSE, cond->flags & EXIT_REPORT_MAP, NULL);
    putchar('\n');
}

int check_exit_condition(struct ping_rts *rts)
{
    if (! _check_exit_condition(rts))
    {
        //if(rts->opt_exit_cond) print_ping_map(rts->opt_exit_cond, TRUE);
        return FALSE;
    }

    //print_exit_cond_report(rts, FALSE);

    rts->opt_exit_cond->condition_met=TRUE; // Set status
    if(global_exit_cond_status == 0) global_exit_cond_status = 1; // Set globsl status if enabled
    return TRUE;
}

static void parse_error(char *opt, int pos, char *msg, ...)
{
    va_list ap;
    char _msgbuf[DEFAULT_MSG_BUFLEN];
    size_t msglen;
    char *msgbuf = _msgbuf;

    va_start(ap, msg);
    msglen = vsnprintf(msgbuf, DEFAULT_MSG_BUFLEN, msg, ap);
    // Do we need a bigger buffer ?
    if (msglen >= DEFAULT_MSG_BUFLEN)
    {
        msgbuf = malloc(msglen + 1);
        vsnprintf(msgbuf, msglen + 1, msg, ap);
    }
    va_end(ap);

    error(2, 0, "-x parsing error '%s'@%d: %s", opt, pos + 1, msgbuf);

    if (msgbuf != _msgbuf)
        free(msgbuf);
}

void parse_args(struct exit_condition *cond, char opt, char *args, int start, int end)
{
    int phase=1;
    size_t mapSize=0;
    char c;

    if(start < 1) return;
    int pos = start-1;

    switch(opt)
    {
        case 'm':
            while(++pos < end)
            {
                c = args[pos];
                switch(phase)
                {
                    case 1:
                        if(isdigit(c)) { mapSize=(mapSize*10)+ (size_t)(c - '0'); continue; }
                        if(c != ':') parse_error(args, pos, "invalid character (%c) in args to option 'm', expecting digit or ':'", c);
                        if(mapSize<1 && pos > start) parse_error(args, pos, "'m' arg map size must be >0");
                        phase++;
                        continue;
                    case 2:
                        if((end - pos) != 2) parse_error(args, pos, "expecting exactely 2 characters after ':' in 'm' args");
                        cond->map_chars[EXIT_COND_MAP_CHAR_SUCCESS_IDX]=args[pos++]; // Success
                        cond->map_chars[EXIT_COND_MAP_CHAR_FAILED_IDX]=args[pos]; // Failure
                        // Here we know, together with error check above that we arrived @end
                        break;
                }
                // New mapSize?
                if(mapSize) cond->max_map_size = mapSize;
            } 
    }
}

int parse_opt(struct exit_condition *cond, char *arg, int pos)
{
    char opt = arg[pos];
    char modifier = ' ';
    int argPos = 0; // 0 is impossible, marks no arg

    // Modifier ?
    if (opt == '-' || opt == '+')
    {
        modifier = opt;
        opt = arg[++pos];
    }
    if (opt == '\0')
        parse_error(arg, pos, "unexpected end of option string", opt);
    if (strchr(EXIT_COND_OPTIONS, opt) == NULL)
        parse_error(arg, pos, "invalid option '%c'", opt);
    if (modifier != ' ' && opt != 'n')
        parse_error(arg, pos, "+/- modifiers only allowed for 'n' option");

    // Arguments ?
    if (arg[pos + 1] == '(')
    {
        pos += 1;         // '('
        argPos = pos + 1; // Start of args
        while (arg[++pos] != ')')
            if (arg[pos] == '\0')
                parse_error(arg, pos, "unexpected end-of-string looking for ')'");
    }

    // Only 'm' supports args
    if (argPos > 0 && opt != 'm')
        parse_error(arg, argPos, "option '%c' does not expect arguments", opt);

    // At this point pos points to the last character of the poarsed option (as expected by caller)
    // and we are ready to parse the specifics
    switch (opt)
    {
    case 'x':
        cond->flags|= EXIT_COND_STATUS;
        global_exit_cond_status = 0; // Enable exit status code, and mark condition not met
        break;
    case 'n':
        if (modifier == ' ')
            modifier = (cond->flags & EXIT_COUNT_FAILED) ? '+' : '-';
        if (modifier == '+')
            cond->flags |= EXIT_REPORT_SUCCESS;
        if (modifier == '-')
            cond->flags |= EXIT_REPORT_FAILURES;
        break;
    case 'N':
        cond->flags |= EXIT_REPORT_SUCCESS | EXIT_REPORT_FAILURES;
        break;
    case 'm':
        cond->flags |= EXIT_REPORT_MAP;
        cond->max_map_size = EXIT_COND_DEFAULT_MAP_SIZE;
        parse_args(cond, opt, arg, argPos, pos);
        extend_ping_map(cond);
        break;
    case 'c':
        cond->flags |= EXIT_REPORT_STATE;
        break;
    case 'q':
        cond->flags |= EXIT_REPORT_SILENT;
        break;
    }

    return pos;
}

struct exit_condition *parse_exit_cond(char *opt)
{
    int pos = -1, bpos = 0;
    int phase = 1;
    char c;
    struct exit_condition *cond = malloc(sizeof(struct exit_condition));

    INIT_EXIT_CONDITION(cond);

    while (opt[++pos])
    {
        c = opt[pos];
        switch (phase)
        {
        case 1:
            // Expected count <n> or -<n>
            if (pos == bpos && c == '-')
            {
                cond->flags |= EXIT_COUNT_FAILED;
                break;
            }
            if (isdigit(c))
            {
                cond->expect = (cond->expect * 10) + (EXIT_COUNTER_TYPE)(c - '0');
                break;
            }
            if (pos == 0)
                parse_error(opt, pos, "unexpected character '%c' at start", c);
            phase++;
            // fall through
        case 2:
            // <loc>
            // currently only 's' supported
            if (c == ':')
            {
                phase++;
                continue;
            }
            if (cond->flags & EXIT_COND_LOC_FLAGS)
                parse_error(opt, pos, "repeat loc flag '%c'", c);
            if (c == 's')
            {
                cond->flags |= EXIT_SEQUENCE;
                continue;
            }
            parse_error(opt, pos, "expect ':', got '%c'", c);
            break;
        case 3:
                // <opt>
                pos = parse_opt(cond, opt, pos);
        }
    }

    if(cond->expect < 1) parse_error(opt,0, "exit condition must define an expected count different from zero");
    return cond;
}

//FIXMEE: if on_exit remains unused we could switch to non-global status
int exit_cond_status_update(int status)
{
    if(status & 0xfffffffe) return status; // Ignore all status different from 0 or 1
    if(global_exit_cond_status>=0) _exit(global_exit_cond_status ? 0 : 1); // Translate condition status to exit code
    return status;
}

void exit_cond_last_gasp(int status, void *arg)
{
    if(global_exit_cond_status < 0) return; // disabled, avoiding to call _exit (paranoia)
    _exit(exit_cond_status_update(status));
}
