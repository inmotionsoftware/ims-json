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
static const char* VER_STR = "1.0.1.0";

//------------------------------------------------------------------------------
#define btomb(bytes) (bytes / (double)(1024*1024))

//------------------------------------------------------------------------------
#define jsonc_assert(B,...) {if(!(B)) {log_err(  __VA_ARGS__ ); exit(EXIT_FAILURE); }}

#define log_debug(...) if (verbose) log_err( "[DEBUG]" __VA_ARGS__ )
#define log_warn(...) log_err( "[WARN]" __VA_ARGS__ )

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
    log_err("ims-jsonc - InMotion Software Json Compiler v%s, libimsjson v%s.", VER_STR, JVER);
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
    log_err("--mem,m                Prints out memory stats.");
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
        {"mem",         no_argument,        0, 'm'},
        {0,0,0,0}
    };

    int verbose = 0;
    int outflags = JPRINT_PRETTY;
    int suppress = 0;
    int memstats = 0;

    int use_stdin = 0;
    FILE* outfile = stdout;

    int idx;
    int c;
    while ((c = getopt_long(argc, argv, "xhio:sufcvm", options, &idx)) != -1)
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

            case 'm':
                memstats = 1;
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
        log_debug("reading from stdin");

        // check for unexpected parameters
        for ( int i = optind; i < argc; i++ )
        {
            log_warn("extra parameter will be ignored: '%s'", argv[i]);
        }
        rt = json_load_file(&jsn, stdin, &err);
    }
    else // read file from path
    {
        jsonc_assert( (argc-optind) > 0, "no input file specified");
        const char* path = argv[optind];

        log_debug("Loading file: '%s'", path);

        jsonc_assert(path && *path, "no input file specified");
        // check for unexpected parameters
        for ( int i = optind+1; i < argc; i++ )
        {
            log_warn("extra parameter will be ignored: '%s'", argv[i]);
        }

        rt = json_load_path(&jsn, path, &err);
    }

    if (rt != 0)
    {
        log_err("%s:%zu:%zu: %s", err.src, err.line+1, err.col, err.msg);
        exit(EXIT_FAILURE);
    }

    // write to disk now (unless we are suppressing output)
    if (!suppress && json_print_file(&jsn, outflags, outfile) == 0)
    {
        log_err("could not write file!");
        exit(EXIT_FAILURE);
    }

    // print memory stats
    if (memstats)
    {
        jmem_stats_t mem = json_get_mem(&jsn);
        json_t jsn;
        json_init(&jsn);
        jobj_t root = json_root_obj(&jsn);
        {
            jobj_t strs = jobj_add_obj(root, "strings");
            {
                jobj_add_int(strs, "used", mem.strs.used);
                jobj_add_int(strs, "reserved", mem.strs.reserved);
            }
            jobj_t nums = jobj_add_obj(root, "nums");
            {
                jobj_add_int(nums, "used", mem.nums.used);
                jobj_add_int(nums, "reserved", mem.nums.reserved);
            }
            jobj_t ints = jobj_add_obj(root, "ints");
            {
                jobj_add_int(ints, "used", mem.ints.used);
                jobj_add_int(ints, "reserved", mem.ints.reserved);
            }
            jobj_t objs = jobj_add_obj(root, "objs");
            {
                jobj_add_int(objs, "used", mem.objs.used);
                jobj_add_int(objs, "reserved", mem.objs.reserved);
            }
            jobj_t arrays = jobj_add_obj(root, "arrays");
            {
                jobj_add_int(arrays, "used", mem.arrays.used);
                jobj_add_int(arrays, "reserved", mem.arrays.reserved);
            }
            jobj_t total = jobj_add_obj(root, "total");
            {
                jobj_add_int(total, "used", mem.total.used);
                jobj_add_int(total, "reserved", mem.total.reserved);
            }
        }
        json_print_file(&jsn, JPRINT_PRETTY, stderr);
        putc('\n', stderr);

        json_destroy(&jsn);
    }

    json_destroy(&jsn);

    return 0;
}
