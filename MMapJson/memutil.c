//
//  memutil.c
//  MMapJson
//
//  Created by Brian Howard on 5/15/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include "memutil.h"
#include "json.h"
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <mach/mach.h>

//------------------------------------------------------------------------------
double btomb(size_t bytes)
{
    return (bytes / (jnum_t)(1024*1024));
}

//------------------------------------------------------------------------------
void print_mem_usage()
{
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);
    printf("[RAM]: Actual: %0.1f MB Virtual: %0.1f MB\n", btomb(t_info.resident_size), btomb(t_info.virtual_size));
}
