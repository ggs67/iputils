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

static void extend_ping_map(struct exit_condition *cond)
{
    // NOTE: we always allocate one additional character to allow
    //       for fast null-terminated string printing
    if(cond->ping_map == NULL)
    {
        cond->map_size = (cond->max_map_size <= EXIT_COND_MAX_INITIAL_MAP_SIZE) ? cond->max_map_size : EXIT_COND_MAX_INITIAL_MAP_SIZE;
#if DEBUG_EXIT_COND >= 5
        printf("DEBUG: creating ping_map with %ld+1 bytes", cond->map_size);
        printf("       max_map_size=%ld, EXIT_COND_MAX_INITIAL_MAP_SIZE=%d", cond->max_map_size, EXIT_COND_MAX_INITIAL_MAP_SIZE);
#endif
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
        printf("DEBUG: extending ping_map from %ld+1 by %d to %ld+1 bytes", old_mapsize, EXIT_COND_MAP_EXTENSION, old_mapsize + EXIT_COND_MAP_EXTENSION);
        if(cond->map_size != old_mapsize + EXIT_COND_MAP_EXTENSION)
            printf("       actual size was reduced to %ld+1 due to limitation by max_map_size=%ld", cond->map_size, cond->max_map_size);
        printf("       max_map_size=%ld, EXIT_COND_MAX_INITIAL_MAP_SIZE=%ld", cond->max_map_size, EXIT_COND_MAX_INITIAL_MAP_SIZE);
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

    if(cond->flags & (EXIT_SEQUENCE | EXIT_REPORT_MAP))
    { //We need the status evaluated in this 2 cases

        // We expect only one counter to have evolved by 1
        if(rts->nreceived == cond->success_seq && rts->nerrors == cond->failed_seq) return 0; // No results for this loop
        assert(cond->success_seq == rts->nreceived || cond->failed_seq == rts->nerrors);
        assert(cond->success_seq == rts->nreceived-1 || cond->failed_seq == rts->nerrors-1);

        if(cond->failed_seq < rts->nerrors)
        {
            cond->failed_seq = rts->nerrors; //FIXIT: according to above tests this could be replaced by ++
            status = FALSE;
        }
        else
        {
            cond->success_seq = rts->nreceived; //FIXIT: according to above tests this could be replaced by ++
            status = TRUE;
        }

        if(cond->flags & EXIT_REPORT_MAP) map_ping(cond, status);
    }

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
            return (rts->nerrors >= cond->expect);
        }
        else
        {
            return (rts->nreceived >= cond->expect);
        }
    }
}

static void print_ping_map(struct exit_condition *cond, int debug)
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
    { /// ...otherwise mapper_pos is end marker
        cond->ping_map[mapper_pos]='\0'; // Terminate map
        fputs(cond->ping_map, stdout);
        // No need to reset appended \0 asthis is not yet part of the map
    }

    putchar('\n');
    if(debug)
    {
        for(int i=0; i < mapper_pos; ++i) putchar(' ');
        putchar('^');
        putchar('\n');
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
    if(rts->opt_exit_cond == NULL) return;
    if(!force && ((rts->opt_exit_cond->flags & EXIT_REPORT_FLAGS) == 0)) return;

    //print_ping_map(rts->opt_exit_cond, TRUE);
    report(rts->opt_exit_cond, TRUE, rts->opt_exit_cond->flags & EXIT_REPORT_SUCCESS, "%d", rts->nreceived);
    report(rts->opt_exit_cond,FALSE, rts->opt_exit_cond->flags & EXIT_REPORT_FAILURES, "%d", rts->nerrors);
    report(rts->opt_exit_cond,FALSE, rts->opt_exit_cond->flags & EXIT_REPORT_MAP, NULL);
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

void exit_cond_last_gasp(int status, void *arg)
{
    if(status & 0xfffffffe) return; // Ignore all status different from 0 or 1
    if(global_exit_cond_status>=0) _exit(global_exit_cond_status ? 0 : 1); // Translate condition status to exit code
}