/*!
    @file main.c
    @author Brian Howard
    @copyright
    Copyright (c) 2015 InMotion Software, LLC.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include <stdio.h>
#include <getopt.h>
#include <stdarg.h>
#include "json.h"

//------------------------------------------------------------------------------
static const char* VER_STR = "1.0.0.0";

//------------------------------------------------------------------------------
#define btomb(bytes) (bytes / (double)(1024*1024))

//------------------------------------------------------------------------------
#define jsonc_assert(B,...) {if(!(B)) {log_err(  __VA_ARGS__ ); exit(EXIT_FAILURE); }}

//------------------------------------------------------------------------------
static void log_err( const char* fmt, ...)
{
    va_list list;
    va_start(list, fmt);
    vfprintf(stderr, fmt, list);
    va_end(list);
    fprintf(stderr, "\n");
}

//------------------------------------------------------------------------------
void print_version()
{
    log_err("jsonc json compiler version \"%s\"", VER_STR);
}

//------------------------------------------------------------------------------
void exit_help(int rt)
{
    log_err("USAGE: ims-jsonc [OPTIONS] jsonfile");
    print_version();
    log_err("--version              print version.");
    log_err("--help,h               print help.");
    log_err("--stdin,i              Read input file from stdin.");
    log_err("--out,o                Write output to file instead of stdout.");
    log_err("--suppress,s           Suppress json output, only validate.");
    log_err("--utf8,u               Escape unicode characters in strings(i.e. \\uXXXX).");
    log_err("--format,f             Format json for human readability with multiple lines and indentions. [default]");
    log_err("--compact,c            Compact output by removing whitespace.");
    log_err("--verbose,v            Verbose logging.");
    exit(rt);
}

//------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    static struct option options[] =
    {
        {"version",     no_argument,        0, 'x'},
        {"help",        no_argument,        0, 'h'},
        {"stdin",       no_argument,        0, 'i'},
        {"out",         required_argument,  0, 'o'},
        {"suppress",    no_argument,        0, 's'},
        {"utf8",        no_argument,        0, 'u'},
        {"format",      no_argument,        0, 'f'},
        {"compact",     no_argument,        0, 'c'},
        {"verbose",     no_argument,        0, 'v'},
        {0,0,0,0}
    };

    int verbose = 0;
    int outflags = JPRINT_PRETTY;
    int suppress = 0;

    int use_stdin = 0;
    FILE* outfile = stdout;

    int idx;
    int c;
    while ((c = getopt_long(argc, argv, "xhio:sufcv", options, &idx)) != -1)
    {
        switch(c)
        {
            case 'x':
                print_version();
                exit(EXIT_SUCCESS);
                break;

            case 'h':
                exit_help(EXIT_SUCCESS);
                break;

            case 'i':
                use_stdin = 1;
                break;

            case 'o':
                jsonc_assert(optarg, "must provide file path for option: --out,o");
                outfile = fopen(optarg, "w");
                jsonc_assert(outfile, "could not open file for output: '%s'", optarg);
                break;

            case 's':
                suppress = 1;
                break;

            case 'u':
                outflags |= JPRINT_ESC_UNI;
                break;

            case 'f':
                outflags |= JPRINT_PRETTY;
                break;

            case 'c':
                outflags &= ~JPRINT_PRETTY;
                break;

            case 'v':
                verbose = 1;
                break;

            case '?':
                break;

            default:
                exit_help(EXIT_FAILURE);
        }
    }

    jerr_t err;
    json_t jsn;
    json_init(&jsn);

    int rt = -1;
    if (use_stdin)
    {
        // check for unexpected parameters
        for ( int i = optind; i < argc; i++ )
        {
            log_err("warning!!! extra parameter will be ignored: '%s'", argv[i]);
        }
        rt = json_load_file(&jsn, stdin, &err);
    }
    else // read file from path
    {
        jsonc_assert( (argc-optind) > 0, "no input file specified");
        const char* path = argv[optind];
        jsonc_assert(path && *path, "no input file specified");
        // check for unexpected parameters
        for ( int i = optind+1; i < argc; i++ )
        {
            log_err("warning!!! extra parameter will be ignored: '%s'", argv[i]);
        }

        rt = json_load_path(&jsn, path, &err);
    }

    if (rt != 0)
    {
        jerr_fprint(stderr, &err);
        exit(EXIT_FAILURE);
    }

    // write to disk now (unless we are suppressing output)
    if (!suppress && json_print_file(&jsn, outflags, outfile) == 0)
    {
        log_err("could not write file!");
        exit(EXIT_FAILURE);
    }

    if (verbose)
    {
        jmem_stats_t mem = json_get_mem(&jsn);
        log_err("[MEM][STRS ]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem.strs.used), btomb(mem.strs.reserved));
        log_err("[MEM][NUMS ]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem.nums.used), btomb(mem.nums.reserved));
        log_err("[MEM][OBJS ]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem.objs.used), btomb(mem.objs.reserved));
        log_err("[MEM][ARRAY]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem.arrays.used), btomb(mem.arrays.reserved));
        log_err("[MEM][TOTAL]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem.total.used), btomb(mem.total.reserved));
    }

    json_destroy(&jsn);

    return 0;
}
