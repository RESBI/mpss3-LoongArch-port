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

#include <stdio.h>
#include <stdlib.h>
    #include <unistd.h>
#include <assert.h>
#include <string.h>
#include <string>
#include <sstream>
#include <vector>
#include <source/COIProcess_source.h>
#include <source/COIEngine_source.h>
#include <internal/_micnativeloadex_version.h>

// Included only for extra debug info for the user.
#include <internal/_Log.h>
#include <internal/_DynamicDependencyFinder.h>
#include <map>

// These are the user input options for the launcher.
//
static bool         opt_verbose   = false;
static int32_t      opt_timeout   = -1;
static char       **opt_app_envv  = NULL;
static uint64_t     opt_app_envc  = 0;
static uint32_t     opt_dev_index = 0;
static char        *opt_app_name  = NULL;
static char       **opt_app_argv  = NULL;
static size_t       opt_app_argc  = 0;
static bool         opt_proxy     = true;

size_t
ConvertStringParam(
    char       *in_string,
    char ***     out_strings,
    bool        in_null_term)
{
    char                      **temp_strings = NULL;
    std::stringstream           tok_stream(in_string);
    std::string                 token;
    std::vector<std::string>    strvec;
    size_t                      total_size = 0;

    while (tok_stream >> token)
    {
        strvec.push_back(token);
    }

    if (!strvec.size())
    {
        *out_strings = NULL;
        return 0;
    }
    total_size = strvec.size();

    // Allocate enough space for all of the tokens plus optionally a NULL
    // string to terminate (used for environment variables).
    //
    if (in_null_term)
    {
        total_size++;
    }
    temp_strings = (char **)malloc(sizeof(char *) * total_size);
    assert(temp_strings);

    if (temp_strings != NULL)
    {
        for (uint64_t i = 0; i < strvec.size(); i++)
        {
            uint64_t length = strvec[i].size();
            temp_strings[i] = (char *)malloc(length + 1);
            assert(temp_strings[i]);

            if (temp_strings[i] == NULL)
            {
                free(temp_strings);
                return -1;
            }
            strncpy(temp_strings[i], strvec[i].c_str(), length + 1);
        }
        if (in_null_term)
        {
            temp_strings[strvec.size()] = NULL;
        }

        *out_strings = temp_strings;
        return total_size;
    }
    return -1;
}

static COIRESULT
PrintDependencies(char *file_name)
{
    // Let's output the missing dependencies to help out the user
    _Elf64_DynamicLibraryFinder finder(file_name, NULL);
    vector<string> found;
    vector<string> missing;
    printf("\nDependency information for %s\n\n", file_name);

    if (!finder.IsValid())
    {
        printf("%s is not a valid file\n", file_name);
        return COI_INVALID_FILE;
    }

    if (finder.GetFullyResolvedFileName() != 0)
    {
        printf("\tFull path was resolved as \n\t%s\n\n",
               finder.GetFullyResolvedFileName());
    }
    std::map<int, std::string> machines;
    machines[Elf64_Ehdr_Machine::EM_X86_64] = "X86_64";
    machines[Elf64_Ehdr_Machine::EM_K1OM] = "Intel(R) Xeon Phi(TM) Coprocessor\n\t(codename: Knights Corner)";

    printf("\tBinary was built for %s architecture\n\n",
           machines[finder.GetMachine()].c_str());
    printf("\tSINK_LD_LIBRARY_PATH = %s\n\n",
           getenv("SINK_LD_LIBRARY_PATH") ? getenv("SINK_LD_LIBRARY_PATH") : "");

    COIRESULT result = finder.GetDynamicLibraryDependencies(found, missing);

    printf("\tDependencies Found:\n");
    if (found.empty())
    {
        printf("\t\t(none found)\n");
    }
    else
    {
        for (vector<string>::iterator iter = found.begin();
                iter != found.end();
                iter++)
        {
            printf("\t\t%s\n", iter->c_str());
        }
    }

    printf("\n\tDependencies Not Found Locally (but may exist already on the coprocessor):\n");
    if (missing.empty())
    {
        printf("\t\t(none missing)\n");
    }
    else
    {
        for (vector<string>::iterator iter = missing.begin();
                iter != missing.end();
                iter++)
        {
            printf("\t\t%s\n", iter->c_str());
        }
    }
    return result;
}

void show_help(char **in_argv)
{

    printf("\nUsage:\n");
    printf("%s [ -h | -V ] AppName -l -t timeout -p -v -d coprocessor -a \"args\" -e \"environment\" \n", in_argv[0]);
    printf("  -a \"args\" An optional string of command line arguments to pass to\n");
    printf("            the remote app.\n");
    printf("  -d The (zero based) index of the Intel(R) Xeon Phi(TM) coprocessor to run the app on.\n");
    printf("  -e \"environment\" An optional environment string to pass to the remote app.\n");
    printf("      Multiple environment variable may be specified using spaces as separators:\n");
    printf("        -e \"LD_LIBRARY_PATH=/lib64/ DEBUG=1\"\n");
    printf("  -h Print this help message\n");
    printf("  -l Do not execute the binary on the coprocessor. Instead, list the shared library\n");
    printf("     dependency information.\n");
    printf("  -p Disable console proxy.\n");
    printf("  -t Time to wait for the remote app to finish (in seconds). After the timeout\n");
    printf("     is reached the remote app will be terminated.\n");
    printf("  -v Enable verbose mode. Note that verbose output will be displayed\n");
    printf("     if the remote app terminates abnormally.\n");
    printf("  -V Show version and build information\n");
    printf("\n");
}


static COIRESULT
ParseCmdLine(
    int                 in_argc,
    char              **in_argv)
{
    int         i;
    int         appIndex = 1;
    COIRESULT   result = COI_ERROR;

    // First parameter must be the application name, -h or -V
    if (in_argc < 2 || in_argv[1][0] == '-')
    {
        if (in_argc < 2)
        {
            printf("Missing remote application name.\n");
            show_help(in_argv);
            return result;
        }
        if (in_argv[1][1] != 'h' && in_argv[1][1] != 'V')
        {
            printf("Invalid parameter order for %s\n", in_argv[1]);
            show_help(in_argv);
            return result;
        }
        else
        {
            //Here we need to push the appIndex out past the first
            //two optional programs so we can process the first two.
            //Doesn't matter if that index really exists as the App
            while (appIndex < in_argc)
                if (in_argv[appIndex][0] == '-')
                    appIndex++;
                else
                    break;
        }
    }

    //This defaults to first parameter, but if any -params
    //are detected then appIndex is incremented until the first
    //non -param.
    opt_app_name = in_argv[appIndex];

    for (i = 1; i < in_argc; i++)
    {
        //Skip processing of the appIndex
        if (i == appIndex) continue;

        //Here we need to get out, if non parametrs are passed in after appIndex
        if (i > appIndex && (in_argv[i][0] != '-' || 1 == strlen(in_argv[i])))
        {
            printf("Invalid option %s.\n", in_argv[i]);
            show_help(in_argv);
            return COI_ERROR;
        }

        switch (in_argv[i][1])
        {
        //Both help and version will immediately exit
        //So that parsing is easier. Checking with
        //other utilities, this seems to be normal
        //behavior
        case 'h' :
            show_help(in_argv);
            exit(0);
            break;

        case 'V' :
            printf("micnativeloadex -- ver: %d.%d\n",
                   MICNATIVELOADEX_MAJOR_VERSION,
                   MICNATIVELOADEX_MINOR_VERSION);
            printf("Built against MPSS stack ver: %s BuildNo: %s\nBuildDate: %s\n\n",
                   MPSS_VERSION, MPSS_BUILDNO, MPSS_BUILTON);
            exit(0);
            break;

        case 'l' :
            // They just want to know the dependencies.
            // Print them and return.
            result = PrintDependencies(opt_app_name);
            if (result != COI_SUCCESS)
            {
                printf("Error printing dependency information\n\n");
                exit(-1);
            }
            else
            {
                exit(0);
            }

        case 't' :
            i++;
            if (i < in_argc)
            {
                opt_timeout = strtol(in_argv[i], NULL, 10) * 1000;
                if (opt_timeout < 1)
                {
                    printf("Timeout value must be > 0\n");
                    show_help(in_argv);
                    return result;
                }
            }
            else
            {
                printf("Missing timeout value for option -t.\n");
                show_help(in_argv);
                return result;
            }
            break;

        case 'd' :
            i++;
            if (i < in_argc)
            {
                opt_dev_index = strtoul(in_argv[i], NULL, 10);
            }
            else
            {
                printf("Missing index value for option -d.\n");
                show_help(in_argv);
                return result;
            }
            break;

        case 'v' :
            opt_verbose = true;
            break;


        case 'p' :
            opt_proxy = false;
            break;

        case 'a' :
            i++;
            if (i < in_argc)
            {
                opt_app_argc =
                    ConvertStringParam(in_argv[i], &opt_app_argv, false);
            }
            else
            {
                printf("Missing argument list for parameter -a.\n");
                show_help(in_argv);
                return result;
            }
            break;

        case 'e' :
            i++;
            if (i < in_argc)
            {
                opt_app_envc =
                    ConvertStringParam(in_argv[i], &opt_app_envv, true);
            }
            else
            {
                printf("Missing environment list for parameter -e.\n");
                show_help(in_argv);
                return result;
            }
            break;

        default :
            printf("Unknown option: %s.\n", in_argv[i]);
            show_help(in_argv);
            return result;
        }
    }

    return COI_SUCCESS;
}


int
main(
    int         argc,
    char      **argv)
{
    COIRESULT               result = COI_ERROR;
    COIPROCESS              proc;
    COIENGINE               engine;
    uint32_t                num_engines = 0;
    int8_t                  sink_return = 0;
    uint32_t                exit_reason = 0;

    if (COI_SUCCESS != ParseCmdLine(argc, argv))
    {
        return -1;
    }

    COI_DEVICE_TYPE BOARD_TYPE = COI_DEVICE_MIC;

    // Check for Intel(R) Xeon Phi(TM) coprocessors
    //
    result = COIEngineGetCount(BOARD_TYPE, &num_engines);
    if (result != COI_SUCCESS)
    {
        printf("Error enumerating Intel(R) Xeon Phi(TM) coprocessors.\n");
        printf("Please verify the Intel(R) Xeon Phi(TM) driver is loaded.\n\n");
        return -1;
    }

    if (num_engines < 1)
    {
        printf("No Intel(R) Xeon Phi(TM) coprocessors found.\n\n");
        return -1;
    }

    if (opt_dev_index > num_engines)
    {
        printf("Invalid Intel(R) Xeon Phi(TM) coprocessor index %u specified. Only %u coprocessors found.\n",
               opt_dev_index, num_engines);
        return -1;
    }

    result = COIEngineGetHandle(BOARD_TYPE, opt_dev_index, &engine);
    if (result != COI_SUCCESS)
    {
        printf("Unable to attach to requested Intel(R) Xeon Phi(TM) coprocessor number %d.\n",
               opt_dev_index);
        printf("Check that the mpss service has been started and that\n");
        printf("the coi_daemon is running on the coprocessor.\n");
        printf("Check your Intel(R) Xeon Phi(TM) documentation for details\n");
        printf("on starting the mpss service and ssh to the coprocessor and run\n");
        printf("ps to verify the coi_daemon is executing.\n");
        printf("It may be necessary to restart the mpss service.\n\n");
        return -1;
    }


    result = COIProcessCreateFromFile(
                 engine,
                 opt_app_name,
                 (int)opt_app_argc, (const char **)opt_app_argv,
                 false, (const char **)opt_app_envv,
                 opt_proxy, NULL,
                 0,
                 NULL,
                 &proc);

    if (result == COI_DOES_NOT_EXIST)
    {
        printf("Supplied binary could not be found.\n\n");
        return -1;
    }
    if (result == COI_INVALID_HANDLE)
    {
        printf("The supplied engine handle is invalid.\n\n");
        return -1;
    }
    if (result == COI_INVALID_POINTER)
    {
        printf("The supplied binary may be NULL.\n\n");
        return -1;
    }
    if (result == COI_INVALID_FILE)
    {
        printf("Either the supplied binary isn't a regular file, or its size is zero.\n\n");
        return -1;
    }
    if (result == COI_ARGUMENT_MISMATCH)
    {
        printf("Either the number of arguments is 0, but there are arguments supplied;\n");
        printf("or the number of arguments is greater than 0, and there are no arguments supplied.\n");
        printf("This may be a result of mistmatched supporting libraries\n\n");
        return -1;
    }
    if (result == COI_OUT_OF_RANGE)
    {
        printf("Either a negative number of arguments was passed in,\n");
        printf("the length of the supplied binary is greater or equal to COI_MAX_FILE_NAME_LENGTH,\n");
        printf("or the the length of the supplied binary is zero.\n\n");
        return -1;
    }
    if (result == COI_MISSING_DEPENDENCY)
    {
        printf("Error creating remote process, at least one library dependency is missing.\n");
        printf("Please check the list of dependencies below to see which\n");
        printf("one is missing and update the SINK_LD_LIBRARY_PATH\n");
        printf("environment variable to include the missing library.\n\n");
        (void)PrintDependencies(opt_app_name);
        return -1;
    }
    if (result == COI_NOT_INITIALIZED)
    {
        printf("Environment variable SINK_LD_TRACE_LOADED_OBJECTS is set to non empty\n");
        printf("string and there are no errors locating the shared library dependencies.\n\n");
        return -1;
    }
    if (result == COI_NOT_SUPPORTED && opt_proxy)
    {
        printf("Proxy IO is not available with statically linked binaries.\n");
        printf("Please disable the proxy and try again.\n\n");
        return -1;
    }
    if (result == COI_BINARY_AND_HARDWARE_MISMATCH)
    {
        printf("Supplied binary does not match the Intel(R) Xeon Phi(TM)\n");
        printf("coprocessor that is installed.\n\n");
        return -1;
    }
    if (result == COI_VERSION_MISMATCH)
    {
        printf("The version of Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI)\n");
        printf("on the host is not compatible with the version on the device.\n\n");
        return -1;
    }
    if (result == COI_TIME_OUT_REACHED)
    {
        printf("Establishing the communication channel with the remote process timed out.\n\n");
        return -1;
    }
    if (result == COI_RESOURCE_EXHAUSTED)
    {
        printf("No more COIProcesses can be created, due to certain resources being exhausted.\n");
        printf("ex: file descriptor count limitation was hit or out of RAM.\n");
        printf("It is possible that in_InitialBufferSpace is too large.\n\n");
        return -1;
    }
    if (result == COI_PROCESS_DIED)
    {
        printf("Some point during the loading of the remote process the remote process terminated abnormally.\n");
        printf("ssh to the coprocessor and run ps to verify the coi_daemon\n");
        printf("is executing.\n");
        printf("It may be necessary to restart the mpss service.\n\n");
        return -1;
    }
    if (result != COI_SUCCESS)
    {
        printf("Unable to create remote process.\n");
        printf("ssh to the coprocessor and run ps to verify the coi_daemon\n");
        printf("is executing.\n");
        printf("It may be necessary to restart the mpss service.\n\n");
        return -1;
    }

    result = COIProcessDestroy(
                 proc,
                 opt_timeout,
                 opt_timeout == -1 ? false : true,
                 &sink_return,
                 &exit_reason
             );
    if (result != COI_SUCCESS)
    {
        printf("Error terminating remote process\n\n");
        return -1;
    }

    for (uint32_t i = 0; i < opt_app_argc; i++)
    {
        free(opt_app_argv[i]);
    }
    free(opt_app_argv);

    for (uint64_t i = 0; i < opt_app_envc; i++)
    {
        free(opt_app_envv[i]);
    }
    free(opt_app_envv);

    if (opt_verbose || (exit_reason != 0))
    {
        printf("Remote process returned: %d\n", sink_return);
        printf("Exit reason: ");
        switch (exit_reason)
        {
        case 0:
            printf("SHUTDOWN OK\n");
            break;
        default:
            printf("Exit reason %d - %s\n",
                   exit_reason, strsignal(exit_reason));
        }
    }

    printf("\n");
    return ((exit_reason == 0) ? sink_return : -1);
}
