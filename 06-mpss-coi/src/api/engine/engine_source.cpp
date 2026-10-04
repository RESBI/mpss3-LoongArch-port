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


#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
    #include <unistd.h>
#include <errno.h>
#include <stdio.h>

    #include <dirent.h>


#include <internal/_Engine.h>
#include <internal/_Daemon.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_Debug.h>
#include <internal/_Log.h>
#include <internal/_SysInfo.h>
#include <internal/coitrace.h>
#include <internal/coi_version_asm.h>


#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif
    static void ReadSysInfo(_COIEngine *, COI_ENGINE_INFO *);

// Assert at compile time that COI_ENGINE_INFO is a valid superset of
// COI_ENGINE_INFO_BASE
STATIC_ASSERT(sizeof(COI_ENGINE_INFO_BASE) <= sizeof(COI_ENGINE_INFO));
STATIC_ASSERT(offsetof(COI_ENGINE_INFO_BASE, SwapMemoryFree) ==
              offsetof(COI_ENGINE_INFO, SwapMemoryFree));
STATIC_ASSERT(sizeof(COI_ENGINE_INFO_BASE) ==
              offsetof(COI_ENGINE_INFO, VendorId));

//=============================================================================
// _Engine Apis -- Start
//=============================================================================

//Retrieves the driver version from the files:
//In Linux systems:        /sys/class/mic/ctrl/version
//In Windows systems:      %INTEL_MPSS_HOME%\drivers\micx64\micx64.inf
COIRESULT GetDriverVersion(coi_wchar_t *DriverVersion)
{
    FILE *dv_fp;
    char c = '\0', endchar;
    int i = 0;

    if (DriverVersion[0] >= (coi_wchar_t)'0' && DriverVersion[0] <= (coi_wchar_t)'9')
    {
        return COI_SUCCESS;
    }

    // Driver version file for KNC
    dv_fp = fopen("/sys/class/mic/ctrl/version", "r");
    // Else: Driver version file for KNL-LB
    if (!dv_fp)
    {
        dv_fp = fopen("/sys/module/mic_x200/version", "r");
    }
    if (dv_fp == NULL)
    {
        return COI_ERROR;
    }

    //Validate that we're looking at the driver version information
    endchar = ' ';

    // We assume that string version should contain only digits and dots.
    // If any invalid character is found during validation,
    // version string contains all up to last found valid character.
    i = 0;
    while ((c = fgetc(dv_fp)) != endchar)
    {
        if ((((c >= '0') && (c <= '9')) || (c == '.')) && (i < COI_MAX_DRIVER_VERSION_STR_LEN))
        {
            DriverVersion[i] = (coi_wchar_t)c;
            i++;
        }
        else
        {
            break;
        }
    }
    if (i == 0)
    {
        fclose(dv_fp);
        return COI_ERROR;
    }

    if (fclose(dv_fp) == EOF)
    {
        memset(DriverVersion, 0, COI_MAX_DRIVER_VERSION_STR_LEN * sizeof(coi_wchar_t));
        return COI_ERROR;
    }

    return COI_SUCCESS;

}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIEngineGetHostname, 1)(
    COIENGINE in_EngineHandle,
    char    *out_Hostname)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    _COIEngine *engine = NULL;

    engine = _COIEngine::Get(in_EngineHandle);
    if (NULL == engine)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == out_Hostname)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if (engine->GetHostname(out_Hostname) != COI_SUCCESS)
    {
        coi_result = COI_ERROR;
    }
    else
    {
        if (out_Hostname == NULL)
        {
            coi_result = COI_ERROR;
        }
        else
        {
            coi_result = COI_SUCCESS;
        }
    }

end:
    if (TRACE_COIEngineGetHostname)
    {
        TRACE_COIEngineGetHostname(in_EngineHandle,
                                   out_Hostname);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIEngineGetInfo, 1)(
    COIENGINE           in_EngineHandle,
    uint32_t            in_EngineInfoSize,
    COI_ENGINE_INFO    *out_pEngineInfo)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    _COIEngine *engine = NULL;

    engine = _COIEngine::Get(in_EngineHandle);
    if (NULL == engine)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == out_pEngineInfo)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    // TODO: In future, if any fields are added to COI_ENGINE_INFO then
    // reproduce the definitions for the old struct here and do sizeof
    // comparisons between them and in_EngineInfoSize.  Then cast
    // out_pEngineInfo to the right one and only fill in the fields that are
    // valid.  For now, there is only one struct and size.
    if (in_EngineInfoSize != sizeof(COI_ENGINE_INFO))
    {
        if (in_EngineInfoSize != sizeof(COI_ENGINE_INFO_SCIF))
        {
            COILOG_FUNC_GOTO_END(COI_SIZE_MISMATCH);
        }
    }

    try
    {
        // Get the daemon-site engine info
        coi_result = engine->GetInfo(out_pEngineInfo, in_EngineInfoSize);

        if (coi_result != COI_SUCCESS)
        {
            COILOG_FUNC_GOTO_END(coi_result);
        }

        if (engine->m_NodeType == COI_SCIF_NODE)
        {
            ReadSysInfo(engine, out_pEngineInfo);
        }

        //Retrieve Driver Version information from the host.
        if (engine->m_NodeType == COI_SCIF_NODE && in_EngineInfoSize == sizeof(COI_ENGINE_INFO))
        {
            out_pEngineInfo->CpuStepping = out_pEngineInfo->BoardStepping;
            if (GetDriverVersion(out_pEngineInfo->DriverVersion) != COI_SUCCESS)
            {
                COILOG_WARNING("Unable to retrieve driver version information");
            }
        }
    }
    catch (...)
    {
        COILOG_FUNC_GOTO_END(COI_ERROR);
    }

    coi_result = COI_SUCCESS;

end:
    if (TRACE_COIEngineGetInfo)
    {
        TRACE_COIEngineGetInfo(in_EngineHandle,
                               in_EngineInfoSize,
                               out_pEngineInfo);
    }
    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIEngineGetCount, 1)(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t           *out_pNumEngines)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    if (NULL == out_pNumEngines)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    coi_result = _COIEngine::GetCount(in_DeviceType, out_pNumEngines);

end:
    if (TRACE_COIEngineGetCount)
    {
        TRACE_COIEngineGetCount(in_DeviceType,
                                out_pNumEngines);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIEngineGetHandle, 1)(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t            in_EngineIndex,
    COIENGINE          *out_pEngineHandle)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    const char *coi_auth_mode_env = NULL;

    if (NULL == out_pEngineHandle)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

#ifdef TRANSPORT_OFI
    coi_auth_mode_env = getenv("COI_AUTH_MODE");
    coi_result = COISecurity::Initialize(coi_auth_mode_env);

    if (coi_result)
    {
        COILOG_FUNC_GOTO_END(coi_result);
    }
#endif
    coi_result = _COIEngine::GetHandle(in_DeviceType,
                                       in_EngineIndex,
                                       out_pEngineHandle);

end:
    if (TRACE_COIEngineGetHandle)
    {
        TRACE_COIEngineGetHandle(in_DeviceType,
                                 in_EngineIndex,
                                 out_pEngineHandle);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}
#define MIC_SYS_PATH_LENGTH 128

// Given a dir "basename" that ends in /
// read the file named "name" and store its contents in "string
static int readfile(char *basename, const char *name, char *string, size_t max)
{
    char    fullname[MIC_SYS_PATH_LENGTH];
    int     fd;

    snprintf(fullname, sizeof(fullname), "%s%s", basename, name);

    fd = open(fullname, O_RDONLY);
    if (-1 == fd)
    {
        return -1;
    }

    read(fd, string, max);

    close(fd);
    return 0;
}

// Read stuff in the /sys/class/mic/mic# dir
static void ReadSysInfo(_COIEngine *engine, COI_ENGINE_INFO *info)
{
    char            basename[MIC_SYS_PATH_LENGTH + 1] = { 0 };
    unsigned int    tempints[4];
    char            tempstring[MIC_SYS_PATH_LENGTH];
    DIR            *dp = NULL;
    struct dirent  *dirent = NULL;
    int             matched = 0;
    int             ret = 0;

    char *end;
    uint64_t addr = strtoull(engine->GetNodeAddress(), &end, 10) - 1;
    uint16_t val = addr;
    snprintf(basename, MIC_SYS_PATH_LENGTH, "/sys/class/mic/mic%d/",
             val);

    ret = readfile(basename, "stepping", tempstring, sizeof(tempstring));
    if (0 == ret)
    {
        matched = sscanf(tempstring, "%x", &tempints[0]);
        info->BoardStepping = (matched == 1) ? tempints[0] : 0;
    }
    else
    {
        info->BoardStepping = 0;
    }

    ret = readfile(basename, "sku", tempstring, sizeof(tempstring));
    if (0 == ret)
    {
        matched = sscanf(tempstring, "%x", &tempints[0]);
        info->BoardSKU = (matched == 1) ? tempints[0] : 0;
    }
    else
    {
        info->BoardSKU = 0;
    }

    if ((dp = opendir(basename)) == NULL)
    {
        return;
    }
    matched = 0;
    while ((dirent = readdir(dp)) != NULL)
    {
        if (strncmp(dirent->d_name, "device", strlen("device")) == 0)
        {
            strncat(basename, dirent->d_name, (MIC_SYS_PATH_LENGTH - 1) - strlen(basename));
            strncat(basename, "/", (MIC_SYS_PATH_LENGTH - 1) - (strlen(basename) - 1));
            matched = 1;
            break;
        }
    }
    closedir(dp);
    if (!matched)
    {
        return;
    }

    ret = readfile(basename, "vendor", tempstring, sizeof(tempstring));
    if (0 == ret)
    {
        matched = sscanf(tempstring, "%x", &tempints[0]);
        info->VendorId = (matched == 1) ? tempints[0] : 0;
    }
    else
    {
        info->VendorId = 0;
    }

    ret = readfile(basename, "device", tempstring, sizeof(tempstring));
    if (0 == ret)
    {
        matched = sscanf(tempstring, "%x", &tempints[0]);
        info->DeviceId = (matched == 1) ? tempints[0] : 0;
    }
    else
    {
        info->DeviceId = 0;
    }

    ret = readfile(basename, "subsystem_device", tempstring, sizeof(tempstring));
    if (0 == ret)
    {
        matched = sscanf(tempstring, "%x", &tempints[0]);
        info->SubSystemId = (matched == 1) ? tempints[0] : 0;
    }
    else
    {
        info->SubSystemId = 0;
    }
}
