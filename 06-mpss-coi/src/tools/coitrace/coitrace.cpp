/*
 * Copyright 2010-2017 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 2.1.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Disclaimer: The codes contained in these modules may be specific
 * to the Intel Software Development Platform codenamed Knights Ferry,
 * and the Intel product codenamed Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel offers no warranty of any kind regarding the code. This code is
 * licensed on an "AS IS" basis and Intel is not obligated to provide
 * any support, assistance, installation, training, or other services
 * of any kind. Intel is also not obligated to provide any updates,
 * enhancements or extensions. Intel specifically disclaims any warranty
 * of merchantability, non-infringement, fitness for any particular
 * purpose, and any other warranty.
 *
 * Further, Intel disclaims all liability of any kind, including but
 * not limited to liability for infringement of any proprietary rights,
 * relating to the use of the code, even if Intel is notified of the
 * possibility of such liability. Except as expressly stated in an Intel
 * license agreement provided with this code and agreed upon with Intel,
 * no license, express or implied, by estoppel or otherwise, to any
 * intellectual property rights is granted herein.
 */

    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/types.h>
    #include <sys/stat.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <internal/_coitrace_version.h>
    #include <sys/types.h>
    #include <linux/limits.h>

#define COITRACE_LIB "libcoitracelib.so.0"

static int      g_count    = 0;
static int      g_all      = 1;
static int      g_buffer   = 0;
static int      g_engine   = 0;
static int      g_event    = 0;
static int      g_pipeline = 0;
static int      g_process  = 0;
static int      g_short    = 0;
static char    *g_file     = NULL;
static char    *g_lib      = NULL;

void PrintHelp()
{
    printf("Usage:\n");
    printf("coitrace <options> coi_binary_source <args>\n");
    printf("\noptions:\n");
    printf("\t -buffer \t Enable buffer trace\n");
    printf("\t -engine \t Enable engine trace\n");
    printf("\t -event \t Enable event trace\n");
    printf("\t -pipeline \t Enable pipeline trace\n");
    printf("\t -process \t Enable process trace\n");
    printf("\t -short \t Only print the API name\n");
    printf("\t -file= \t File to log to, can also be stdout or stderr\n");
    printf("\t -lib= \t\t Path to the libcoitracelib.so\n");
    printf("\t -h \t\t Print this help message\n");
    printf("\t -V \t\t Show the version and build information");
    printf("\nCurrently traces some API calls on API exit rather than entry\n"
           " to help debug output params.\n");
    exit(0);
}

void PrintVersion()
{
    printf("coitrace -- ver: %d.%d\n",
           COITRACE_MAJOR_VERSION,
           COITRACE_MINOR_VERSION);
    printf("Built against MPSS stack version: %s BuildNo: %s BuildDate: %s\n\n",
           MPSS_VERSION, MPSS_BUILDNO, MPSS_BUILTON);

    exit(0);
}

//Set g_lib to the default path of libcoitracelib.so.0
//Return -1 if the library cannot be found
//Return 0 on success
int SetLibDefaultLinux()
{
    char *temp;
    char lib64[PATH_MAX] = "../lib64/";
    char lib[PATH_MAX] = "../lib/";
    char libexec[PATH_MAX] = "../libexec/";
    char local_lib64[PATH_MAX] = "../local/lib64/";
    char local_lib[PATH_MAX] = "../local/lib/";
    char local_libexec[PATH_MAX] = "../local/libexec/";

    //To begin, make temp point to the absolute path of the coitrace binary
    //we operate later on this part of memory using strcat & strcat so it
    //should has enough size
    g_lib = (char *)calloc(PATH_MAX * 2 + sizeof(COITRACE_LIB), sizeof(char));
    if (!g_lib)
    {
        return -1;
    }

    if (readlink("/proc/self/exe", g_lib, PATH_MAX) < 1)
    {
        return -1;
    }

    temp = rindex(g_lib, '/');
    if (!temp)
    {
        return -1;
    }
    temp++;

    //See if the library is in the same directory as coitrace
    strcpy(temp, COITRACE_LIB);
    if (access(g_lib, F_OK) == 0)
    {
        return 0;
    }

    //It isn't, so try some other locations before throwing a fit
    strcpy(temp, strcat(lib64, COITRACE_LIB));
    if (access(g_lib, F_OK) == 0)
    {
        return 0;
    }
    strcpy(temp, strcat(lib, COITRACE_LIB));
    if (access(g_lib, F_OK) == 0)
    {
        return 0;
    }
    strcpy(temp, strcat(libexec, COITRACE_LIB));
    if (access(g_lib, F_OK) == 0)
    {
        return 0;
    }
    strcpy(temp, strcat(local_lib64, COITRACE_LIB));
    if (access(g_lib, F_OK) == 0)
    {
        return 0;
    }
    strcpy(temp, strcat(local_lib, COITRACE_LIB));
    if (access(g_lib, F_OK) == 0)
    {
        return 0;
    }
    strcpy(temp, strcat(local_libexec, COITRACE_LIB));
    if (access(g_lib, F_OK) == 0)
    {
        return 0;
    }

    return -1;
}


void ParseCmdLine(int argc, char **argv)
{
    int i;

    if (argc == 1)
    {
        PrintHelp();
    }

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            break;
        }
        if (argv[i][1] == '-')
            argv[i]++;

        if (strncmp(argv[i], "-h", strlen("-h")) == 0)
        {
            PrintHelp();
        }
        else if (strncmp(argv[i], "-V", strlen("-V")) == 0)
        {
            PrintVersion();
        }
        else if (strncmp(argv[i], "-buffer", strlen("-buffer")) == 0)
        {
            g_buffer = 1;
        }
        else if (strncmp(argv[i], "-engine", strlen("-engine")) == 0)
        {
            g_engine = 1;
        }
        else if (strncmp(argv[i], "-event", strlen("-event")) == 0)
        {
            g_event = 1;
        }
        else if (strncmp(argv[i], "-pipeline", strlen("-pipeline")) == 0)
        {
            g_pipeline = 1;
        }
        else if (strncmp(argv[i], "-process", strlen("-process")) == 0)
        {
            g_process = 1;
        }

        else if (strncmp(argv[i], "-short", strlen("-short")) == 0)
        {
            g_short = 1;
        }
        else if (strncmp(argv[i], "-file", strlen("-file")) == 0)
        {
            if (!g_file)
            {
                g_file = (char *)malloc(strlen(argv[i]) - strlen("-file=") + 1);
                if (!g_file)
                {
                    exit(-1);
                }
            }
            else
            {
                fprintf(stderr, "-file can only be specified once\n");
                exit(-1);
            }
            strcpy(g_file, &argv[i][strlen("-file=")]);
        }
        else if (strncmp(argv[i], "-lib", strlen("-lib")) == 0)
        {
            g_lib = (char *)malloc(strlen(argv[i]) - strlen("-lib=") + 1);
            if (!g_lib)
            {
                exit(-1);
            }
            strcpy(g_lib, &argv[i][strlen("-lib=")]);
        }
        else
        {
            PrintHelp();
        }
    }
    g_count = i;

    //Ensure that the binary exists
    std::ifstream file(argv[g_count]);
    if (!file.is_open())
    {
        fprintf(stderr, "Binary not found\n");
        file.close();
        PrintHelp();
    }
    file.close();

    if (g_buffer || g_engine || g_event || g_pipeline || g_process)
    {
        g_all = 0;
    }
    if (g_all)
    {
        g_buffer = 1;
        g_engine = 1;
        g_event = 1;
        g_pipeline = 1;
        g_process = 1;
    }

    if (!g_lib && SetLibDefaultLinux() != 0)
    {
        fprintf(stderr, "Could not locate '%s'\n", COITRACE_LIB);
        exit(-1);
    }
}

int main(int argc, char **argv)
{
    ParseCmdLine(argc, argv);

    pid_t pid;

    pid = fork();
    if (pid < 0)
    {
        return -1;
    }
    else if (pid > 0)
    {
        wait(NULL);
    }
    else
    {
        char *preload = NULL;
        if (g_buffer)   setenv("COITRACE_BUFFER",   "1", 1);
        if (g_engine)   setenv("COITRACE_ENGINE",   "1", 1);
        if (g_event)    setenv("COITRACE_EVENT",    "1", 1);
        if (g_pipeline) setenv("COITRACE_PIPELINE", "1", 1);
        if (g_process)  setenv("COITRACE_PROCESS",  "1", 1);
        if (g_short)    setenv("COITRACE_SHORT",    "1", 1);
        if (g_file)     setenv("COITRACE_FILE", g_file, 1);
        if ((preload = getenv("LD_PRELOAD")))
        {
            g_lib = (char *)realloc(g_lib,
                                    strlen(g_lib) + 1 + strlen(preload) + 1);
            if (!g_lib)
            {
                return -1;
            }
            strcat(g_lib, ":");
            strcat(g_lib, preload);
        }
        setenv("LD_PRELOAD", g_lib, 1);
        execv(argv[g_count], &argv[g_count]);
        free(g_lib);
    }
    return 0;
}
