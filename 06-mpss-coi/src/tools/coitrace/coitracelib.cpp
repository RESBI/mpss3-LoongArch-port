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

#include <internal/coitrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <iomanip>

static int      g_buffer;
static int      g_engine;
static int      g_event;
static int      g_pipeline;
static int      g_process;
static int      g_short;
static char    *g_file;
static FILE    *g_fPtr;

using namespace std;


    #define LINKAGE               /* extern linkage on linux */


LINKAGE void __attribute__((constructor)) setup(void)
{
    //Set all static variables here, since they could be
    //reinitialized twice in the global setting.
    g_buffer    = 0;
    g_engine    = 0;
    g_event     = 0;
    g_pipeline  = 0;
    g_process   = 0;
    g_short     = 0;
    g_file      = NULL;
    g_fPtr      = stdout;


    if (getenv("COITRACE_BUFFER"))   g_buffer   = 1;
    if (getenv("COITRACE_ENGINE"))   g_engine   = 1;
    if (getenv("COITRACE_EVENT"))    g_event    = 1;
    if (getenv("COITRACE_PIPELINE")) g_pipeline = 1;
    if (getenv("COITRACE_PROCESS"))  g_process  = 1;
    if (getenv("COITRACE_SHORT"))    g_short    = 1;
    g_file = getenv("COITRACE_FILE");

    if (g_file)
    {
        if (strcmp(g_file, "stdout") == 0)
        {
            g_fPtr = stdout;
        }
        else if (strcmp(g_file, "stderr") == 0)
        {
            g_fPtr = stderr;
        }
        else
        {
            g_fPtr = fopen(g_file, "w");
            if (!g_fPtr)
            {
                g_fPtr = stdout;
            }
        }
    }
}

void __attribute__((destructor)) cleanup(void)
{
    fflush(g_fPtr);
    fclose(g_fPtr);
}

// The ConvertParam template functions handle converting data types to strings.
// The generic function below handles default cases, converting C base types
// like all of the different flavors of ints, into strings.
// The rest of the functions are specialized for specific data types such as
// Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI)
// handles or or Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI)
// enums. This can also be used to do a deep print of a
// struct such as the COI_ENGINE_INFO structure.
// To use these just pass in the ostringstream that you are building along with
// the object to convert and the compiler will figure out which one to use.
//
template<class T>
void ConvertParam(ostringstream &s, T val)
{
    s << val;
}

template<>
void ConvertParam<uint64_t>(ostringstream &s, uint64_t val)
{
    s << "0x" << std::hex << std::setw(16) << std::setfill('0') << val << " (hex)";
    s << " : " << std::dec << val << " (dec)";
}

template<>
void ConvertParam<uint32_t>(ostringstream &s, uint32_t val)
{

    s << "0x" << std::hex << std::setw(8) << std::setfill('0') << val << " (hex)";
    s << " : " << std::dec << val << " (dec)";
}

template<>
void ConvertParam<bool>(ostringstream &s, bool val)
{
    if (val)
    {
        s << "true";
    }
    else
    {
        s << "false";
    }
}

// Fortunately, output stream understands void* and
// will output 0x(HEX). Otherwise, you would need
// to do something like
// s << std::hex << "0x" << (unsigned long long)val << std::dec;
template<>
void ConvertParam<void *>(ostringstream &s, void *val)
{
    s << val;
}

template<>
void ConvertParam<COIPROCESS>(ostringstream &s, COIPROCESS val)
{
    ConvertParam(s, (void *)val);
}

template<>
void ConvertParam<COIPIPELINE>(ostringstream &s, COIPIPELINE val)
{
    ConvertParam(s, (void *)val);
}

template<>
void ConvertParam<COIFUNCTION>(ostringstream &s, COIFUNCTION val)
{
    ConvertParam(s, (void *)val);
}

template<>
void ConvertParam<COIENGINE>(ostringstream &s, COIENGINE val)
{
    ConvertParam(s, (void *)val);
}

template<>
void ConvertParam<COIEVENT>(ostringstream &s, COIEVENT val)
{
    s << (long long)val.opaque[0] << ":" << (long long)val.opaque[1];
}

template<>
void ConvertParam<COIBUFFER>(ostringstream &s, COIBUFFER val)
{
    ConvertParam(s, (void *)val);
}

template<>
void ConvertParam<COI_DEVICE_TYPE>(ostringstream &s, COI_DEVICE_TYPE val)
{
    switch (val)
    {
    case COI_DEVICE_SOURCE:
        s << "COI_DEVICE_SOURCE";
        break;
    case COI_DEVICE_MIC:
        s << "COI_DEVICE_MIC";
        break;
    case COI_DEVICE_KNF:
        s << "COI_DEVICE_KNF (DEPRECATED)";
        break;
    case COI_DEVICE_KNC:
        s << "COI_DEVICE_KNC";
        break;
    case COI_DEVICE_KNL:
        s << "COI_DEVICE_KNL";
        break;
    default:
        s << "Unknown COI_DEVICE_TYPE " << val;
    }
}

template<>
void ConvertParam<COI_MAP_TYPE>(ostringstream &s, COI_MAP_TYPE val)
{
    switch (val)
    {
    case COI_MAP_READ_WRITE:
        s << "COI_MAP_READ_WRITE";
        break;
    case COI_MAP_READ_ONLY:
        s << "COI_MAP_READ_ONLY";
        break;
    case COI_MAP_WRITE_ENTIRE_BUFFER:
        s << "COI_MAP_WRITE_ENTIRE_BUFFER";
        break;
    default:
        s << "Unknown COI_MAP_TYPE " << val;
    }
}

template<>
void ConvertParam<COI_BUFFER_TYPE>(ostringstream &s, COI_BUFFER_TYPE val)
{
    switch (val)
    {
    case COI_BUFFER_NORMAL:
        s << "COI_BUFFER_NORMAL";
        break;
    case COI_BUFFER_OPENCL:
        s << "COI_BUFFER_OPENCL";
        break;
    default:
        s << "Unknown COI_BUFFER_TYPE " << val;
    }
}

template<>
void ConvertParam<COI_COPY_TYPE>(ostringstream &s, COI_COPY_TYPE val)
{
    switch (val)
    {
    case COI_COPY_UNSPECIFIED:
        s << "COI_COPY_UNSPECIFIED";
        break;
    case COI_COPY_USE_DMA:
        s << "COI_COPY_USE_DMA";
        break;
    case COI_COPY_USE_CPU:
        s << "COI_COPY_USE_CPU";
        break;
    default:
        s << "Unknown COI_COPY_TYPE " << val;
    }
}

template<>
void ConvertParam<COI_ACCESS_FLAGS>(ostringstream &s, COI_ACCESS_FLAGS val)
{
    switch (val)
    {
    case COI_SINK_READ:
        s << "COI_SINK_READ";
        break;
    case COI_SINK_WRITE:
        s << "COI_SINK_WRITE";
        break;
    case COI_SINK_WRITE_ENTIRE:
        s << "COI_SINK_WRITE_ENTIRE";
        break;
    case COI_SINK_READ_ADDREF:
        s << "COI_SINK_READ_ADDREF";
        break;
    case COI_SINK_WRITE_ADDREF:
        s << "COI_SINK_WRITE_ADDREF";
        break;
    case COI_SINK_WRITE_ENTIRE_ADDREF:
        s << "COI_SINK_WRITE_ENTIRE_ADDREF";
        break;
    default:
        s << "Unknown COI_ACCESS_FLAG " << val;
    }
}

template<>
void ConvertParam<COI_BUFFER_MOVE_FLAG>(ostringstream &s, COI_BUFFER_MOVE_FLAG val)
{
    switch (val)
    {
    case COI_BUFFER_MOVE:
        s << "COI_BUFFER_MOVE";
        break;
    case COI_BUFFER_NO_MOVE:
        s << "COI_BUFFER_NO_MOVE";
        break;
    default:
        s << "Unknown COI_BUFFER_MOVE_FLAG " << val;
    }
}

template<>
void ConvertParam<COI_BUFFER_STATE>(ostringstream &s, COI_BUFFER_STATE val)
{
    switch (val)
    {
    case COI_BUFFER_VALID:
        s << "COI_BUFFER_VALID";
        break;
    case COI_BUFFER_INVALID:
        s << "COI_BUFFER_INVALID";
        break;
    case COI_BUFFER_VALID_MAY_DROP:
        s << "COI_BUFFER_VALID_MAY_DROP";
        break;
    case COI_BUFFER_RESERVED:
        s << "COI_BUFFER_RESERVED";
        break;
    default:
        s << "Unknown COI_BUFFER_STATE " << val;
    }
}

template<>
void ConvertParam<COI_DMA_MODE>(ostringstream &s, COI_DMA_MODE val)
{
    switch (val)
    {
    case COI_DMA_MODE_SINGLE:
        s << "COI_DMA_MODE_SINGLE";
        break;
    case COI_DMA_MODE_READ_WRITE:
        s << "COI_DMA_MODE_READ_WRITE";
        break;
    case COI_DMA_MODE_ROUND_ROBIN:
        s << "COI_DMA_MODE_ROUND_ROBIN";
        break;
    case COI_DMA_RESERVED:
        s << "COI_DMA_RESERVED";
        break;
    default:
        s << "Unknown COI_DMA_MODE " << val;
    }
}

// TODO: Need to print out the rest of the enginfo struct
template<>
void ConvertParam<COI_ENGINE_INFO>(ostringstream &s, COI_ENGINE_INFO val)
{
    s << endl << "\t\tDriverVersion: " << endl;
    s << "\t\tDeviceType: ";
    ConvertParam(s, val.ISA);
    s << endl;
    s << "\t\tNumCores: " << val.NumCores << endl;
    s << "\t\tNumThreads: " << val.NumThreads << endl;
}

// The following macros handle the output for the different parameter classes.
// These append the name of the variable as well as the value to the
// ostringstream passed in.  How the value of the variable is formatted depends
// on which macro is used. These use the Convert* template functions above to
// convert the value of the variable into a string.
// The REG macro just prints a single value.
// The PTR macro prints the address of the pointer as well as what the value
// is at that address. It will only dereference the pointer if it is not NULL.
// The STR macro will display the entire contents of the string if it is not
// NULL.
// The ARY macro will list the contents of an array, if the array is not NULL.
// The BUF macro just prints the pointer value.
//
#define LOG_REG_PARAM(_S, _P) \
    ({ \
        int temp = 0; \
        _S << "\t" << #_P << " = "; \
        ConvertParam(_S, _P); \
        _S << endl; \
        temp; \
    })
#define LOG_PTR_PARAM(_S, _P) \
    ({ \
        int temp = 0; \
        if (_P) \
        { \
            _S << "\t" << #_P << " = " << (void*)_P << " "; \
            ConvertParam(_S, *_P); \
        } \
        else \
        { \
            _S << "\t" << #_P << " = 0x" << (void*)_P << " "; \
        }\
        _S << endl; \
        temp; \
    })
#define LOG_STR_PARAM(_S, _P) \
    ({ \
        int temp = 0; \
        _S << "\t" << #_P << " = "; \
        if (_P) \
        { \
            _S << _P; \
        } \
        else \
        { \
            _S << "(nil)"; \
        } \
        _S << endl; \
        temp; \
    })
#define LOG_ARY_PARAM(_S, _P, _L) \
    ({ \
        unsigned int i = 0; \
        _S << "\t" << #_P << " = "; \
        if (!_P) \
        { \
            _S << "(nil)"; \
        } \
        else \
        { \
            for (i = 0; i < _L; i++) \
            { \
                ConvertParam(_S, _P[i]); \
                if (i != (_L - 1)) \
                { \
                    _S << ", "; \
                } \
            } \
        } \
        _S << endl; \
        i; \
    })
#define LOG_BUF_PARAM(_S, _P, _L) \
    ({ \
        int temp = 0; \
        _S << "\t" << #_P << ((_P)?(" = "):(" = 0x")) << (void*)_P;\
        _S << " (" << _L << " bytes)" ; \
        _S << endl; \
        temp; \
    })

// You can't dereference a true void*, the address is
// the only thing we can print.
#define LOG_VOIDSTAR_PARAM(_S, _P) \
    ({ \
        int temp = 0; \
        _S << "\t" << #_P << " = " << (void*)_P << " "; \
        _S << endl; \
        temp; \
    })

// Convert the wrapper function name to the underlying
// Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI)  function name.
// This assumes that the wrapper has the format TestWrapper_COIFunc and that
// COIFunc maps directly to the actual COI function.
//
#define GetCOIFunc() \
    ({ \
        string func; \
        string temp = __FUNCTION__; \
        func = temp.substr(temp.find("COI")); \
        func; \
    })

void
TRACE_COIBufferCreate(
    uint64_t              in_Size,
    COI_BUFFER_TYPE       in_Type,
    uint32_t              in_Flags,
    const   void                 *in_pInitData,
    uint32_t              in_NumProcesses,
    const   COIPROCESS           *in_pProcesses,
    COIBUFFER            *out_pBuffer)
{
    if (!g_buffer) return;
    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Size);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_Flags);
        LOG_BUF_PARAM(msg, in_pInitData, ((in_pInitData == NULL) ? 0 : in_Size));
        LOG_REG_PARAM(msg, in_NumProcesses);
        LOG_ARY_PARAM(msg, in_pProcesses, in_NumProcesses);
        LOG_PTR_PARAM(msg, out_pBuffer);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferCreateFromMemory(
    uint64_t            in_Size,
    COI_BUFFER_TYPE     in_Type,
    uint32_t            in_Flags,
    void               *in_Memory,
    uint32_t            in_NumProcesses,
    const   COIPROCESS         *in_pProcesses,
    COIBUFFER          *out_pBuffer)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Size);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_Flags);
        LOG_BUF_PARAM(msg, in_Memory, in_Size);
        LOG_REG_PARAM(msg, in_NumProcesses);
        LOG_ARY_PARAM(msg, in_pProcesses, in_NumProcesses);
        LOG_PTR_PARAM(msg, out_pBuffer);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferDestroy(
    COIBUFFER             in_Buffer)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Buffer);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferReleaseRefcnt(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t            in_ReleaseRefcnt)
{

    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_Buffer);
        LOG_REG_PARAM(msg, in_ReleaseRefcnt);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferAddRefcnt(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t            in_AddRefcnt)
{

    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_Buffer);
        LOG_REG_PARAM(msg, in_AddRefcnt);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferMap(
    COIBUFFER           in_Buffer,
    uint64_t            in_Offset,
    uint64_t            in_Length,
    COI_MAP_TYPE        in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion,
    COIMAPINSTANCE     *out_pMapInstance,
    void              **out_ppData)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;


    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Buffer);
        LOG_REG_PARAM(msg, in_Offset);
        LOG_REG_PARAM(msg, in_Length);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
        LOG_PTR_PARAM(msg, out_pMapInstance);
        LOG_PTR_PARAM(msg, out_ppData);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferUnmap(
    COIMAPINSTANCE    in_MapInstance,
    uint32_t          in_NumDependencies,
    const   COIEVENT         *in_pDependencies,
    COIEVENT         *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;


    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_MapInstance);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferGetSinkAddress(
    COIBUFFER           in_Buffer,
    uint64_t           *out_pAddress)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;
    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Buffer);
        LOG_PTR_PARAM(msg, out_pAddress);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferGetSinkAddressEx(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t           *out_pAddress)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;
    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_Buffer);
        LOG_PTR_PARAM(msg, out_pAddress);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}


void
TRACE_COIBufferWriteEx(
    COIBUFFER           in_DestBuffer,
    const   COIPROCESS          in_DestProcess,
    uint64_t            in_Offset,
    const   void               *in_pSourceData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;
    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_DestBuffer);
        LOG_REG_PARAM(msg, in_DestProcess);
        LOG_REG_PARAM(msg, in_Offset);
        LOG_BUF_PARAM(msg, in_pSourceData, in_Length);
        LOG_REG_PARAM(msg, in_Length);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferWriteMultiD(
    COIBUFFER           in_DestBuffer,
    const   COIPROCESS          in_DestProcess,
    uint64_t            in_Offset,
    struct arr_desc    *in_DestArray,
    struct arr_desc    *in_SrcArray,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;
    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_DestBuffer);
        LOG_REG_PARAM(msg, in_DestProcess);
        LOG_REG_PARAM(msg, in_Offset);
        LOG_REG_PARAM(msg, in_DestArray);
        LOG_REG_PARAM(msg, in_SrcArray);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferWrite(
    COIBUFFER           in_DestBuffer,
    uint64_t            in_Offset,
    const   void               *in_pSourceData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_DestBuffer);
        LOG_REG_PARAM(msg, in_Offset);
        LOG_BUF_PARAM(msg, in_pSourceData, in_Length);
        LOG_REG_PARAM(msg, in_Length);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferRead(
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_Offset,
    void               *in_pDestData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_SourceBuffer);
        LOG_REG_PARAM(msg, in_Offset);
        LOG_BUF_PARAM(msg, in_pDestData, in_Length);
        LOG_REG_PARAM(msg, in_Length);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferReadMultiD(
    COIBUFFER          in_SourceBuffer,
    uint64_t           in_Offset,
    struct arr_desc   *in_DestArray,
    struct arr_desc   *in_SrcArray,
    COI_COPY_TYPE      in_Type,
    uint32_t           in_NumDependencies,
    const   COIEVENT          *in_pDependencies,
    COIEVENT          *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_SourceBuffer);
        LOG_REG_PARAM(msg, in_Offset);
        LOG_REG_PARAM(msg, in_DestArray);
        LOG_REG_PARAM(msg, in_SrcArray);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}


void
TRACE_COIBufferCopyEx(
    COIBUFFER           in_DestBuffer,
    const   COIPROCESS          in_DestProcess,
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_DestOffset,
    uint64_t            in_SourceOffset,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;
    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_DestBuffer);
        LOG_REG_PARAM(msg, in_DestProcess);
        LOG_REG_PARAM(msg, in_SourceBuffer);
        LOG_REG_PARAM(msg, in_DestOffset);
        LOG_REG_PARAM(msg, in_SourceOffset);
        LOG_REG_PARAM(msg, in_Length);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferCopy(
    COIBUFFER           in_DestBuffer,
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_DestOffset,
    uint64_t            in_SourceOffset,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_DestBuffer);
        LOG_REG_PARAM(msg, in_SourceBuffer);
        LOG_REG_PARAM(msg, in_DestOffset);
        LOG_REG_PARAM(msg, in_SourceOffset);
        LOG_REG_PARAM(msg, in_Length);
        LOG_REG_PARAM(msg, in_Type);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferSetState(
    COIBUFFER               in_Buffer,
    COIPROCESS              in_Process,
    COI_BUFFER_STATE        in_State,
    COI_BUFFER_MOVE_FLAG    in_DataMove,
    uint32_t                in_NumDependencies,
    const   COIEVENT               *in_pDependencies,
    COIEVENT               *out_pCompletion)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Buffer);
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_State);
        LOG_REG_PARAM(msg, in_DataMove);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIBufferCreateSubBuffer(
    COIBUFFER   in_Buffer,
    uint64_t    in_Length,
    uint64_t    in_Offset,
    COIBUFFER  *out_pSubBuffer)
{
    if (!g_buffer) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Buffer);
        LOG_REG_PARAM(msg, in_Length);
        LOG_REG_PARAM(msg, in_Offset);
        LOG_PTR_PARAM(msg, out_pSubBuffer);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEngineGetIndex(
    COI_DEVICE_TYPE    *out_pType,
    uint32_t           *out_pIndex)
{
    if (!g_engine) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;

    if (!g_short)
    {
        LOG_PTR_PARAM(msg, out_pType);
        LOG_PTR_PARAM(msg, out_pIndex);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEngineGetInfo(
    COIENGINE           in_EngineHandle,
    uint32_t            in_EngineInfoSize,
    COI_ENGINE_INFO    *out_pEngineInfo)
{
    if (!g_engine) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_EngineHandle);
        LOG_REG_PARAM(msg, in_EngineInfoSize);
        LOG_PTR_PARAM(msg, out_pEngineInfo);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEngineGetCount(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t           *out_pNumEngines)
{
    if (!g_engine) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_DeviceType);
        LOG_PTR_PARAM(msg, out_pNumEngines);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEngineGetHandle(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t            in_EngineIndex,
    COIENGINE          *out_pEngineHandle)
{
    if (!g_engine) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_DeviceType);
        LOG_REG_PARAM(msg, in_EngineIndex);
        LOG_PTR_PARAM(msg, out_pEngineHandle);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEngineGetHostname(
    COIENGINE           in_EngineHandle,
    char               *out_Hostname)
{
    if (!g_engine) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_EngineHandle);
        LOG_REG_PARAM(msg, out_Hostname);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEventSignalUserEvent(
    COIEVENT            in_Event)
{
    if (!g_event) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Event);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEventRegisterCallback(
    const COIEVENT                in_Event,
    COI_EVENT_CALLBACK      in_Callback,
    const void                   *in_UserData,
    const uint64_t                in_Flags)
{
    if (!g_event) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Event);
        LOG_REG_PARAM(msg, in_Callback);
        LOG_REG_PARAM(msg, in_UserData);
        LOG_REG_PARAM(msg, in_Flags);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}



void
TRACE_COIEventWait(
    uint16_t        in_NumEvents,
    const   COIEVENT       *in_pEvents,
    int32_t         in_Timeout,
    uint8_t         in_WaitForAll,
    uint32_t       *out_pNumSignaled,
    uint32_t       *out_pSignaledIndices)
{
    if (!g_event) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_NumEvents);
        LOG_ARY_PARAM(msg, in_pEvents, (uint32_t)in_NumEvents);
        LOG_REG_PARAM(msg, in_Timeout);
        LOG_REG_PARAM(msg, (bool) in_WaitForAll);
        LOG_PTR_PARAM(msg, out_pNumSignaled);
        LOG_ARY_PARAM(msg, out_pSignaledIndices, *out_pNumSignaled);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEventRegisterUserEvent(
    COIEVENT       *out_pEvent)
{
    if (!g_event) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_PTR_PARAM(msg, out_pEvent);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIEventUnregisterUserEvent(
    COIEVENT        in_Event)
{
    if (!g_event) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Event);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIPipelineCreate(
    COIPROCESS          in_Process,
    COI_CPU_MASK        in_Mask,
    uint32_t            in_StackSize,
    COIPIPELINE        *out_pPipeline)
{
    if (!g_pipeline) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_ARY_PARAM(msg, in_Mask, sizeof(COI_CPU_MASK) / sizeof(in_Mask[0]));
        LOG_REG_PARAM(msg, in_StackSize);
        LOG_PTR_PARAM(msg, out_pPipeline);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIPipelineDestroy(
    COIPIPELINE         in_Pipeline)
{
    if (!g_pipeline) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Pipeline);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIPipelineRunFunction(
    COIPIPELINE         in_Pipeline,
    COIFUNCTION         in_Function,
    uint32_t            in_NumBuffers,
    const   COIBUFFER          *in_Buffers,
    const   COI_ACCESS_FLAGS   *in_pBufferAccessFlags,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    const   void               *in_pMiscData,
    uint16_t            in_MiscDataLen,
    void               *out_pAsyncReturnValue,
    uint16_t            in_AsyncReturnValueLen,
    COIEVENT           *out_pCompletion)
{
    if (!g_pipeline) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Pipeline);
        LOG_REG_PARAM(msg, in_Function);
        LOG_REG_PARAM(msg, in_NumBuffers);
        LOG_ARY_PARAM(msg, in_Buffers, in_NumBuffers);
        LOG_ARY_PARAM(msg, in_pBufferAccessFlags, in_NumBuffers);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_BUF_PARAM(msg, in_pMiscData, in_MiscDataLen);
        LOG_REG_PARAM(msg, in_MiscDataLen);
        LOG_VOIDSTAR_PARAM(msg, out_pAsyncReturnValue);
        LOG_REG_PARAM(msg, in_AsyncReturnValueLen);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIPipelineGetEngine(
    COIPIPELINE         in_Pipeline,
    COIENGINE          *out_pEngine)
{
    if (!g_pipeline) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Pipeline);
        LOG_PTR_PARAM(msg, out_pEngine);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIPipelineSetCPUMask(
    COIPROCESS          in_Process,
    uint32_t            in_CoreID,
    uint8_t             in_ThreadID,
    COI_CPU_MASK       *out_pMask)
{
    if (!g_pipeline) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_CoreID);
        LOG_REG_PARAM(msg, (uint32_t) in_ThreadID);
        LOG_PTR_PARAM(msg, out_pMask);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIPipelineClearCPUMask(
    COI_CPU_MASK       *in_Mask)
{
    if (!g_pipeline) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_ARY_PARAM(msg, in_Mask, sizeof(COI_CPU_MASK) / sizeof(in_Mask));
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessCreateFromMemory(
    COIENGINE           in_Engine,
    const   char               *in_pBinaryName,
    const   void               *in_pBinaryBuffer,
    uint64_t            in_BinaryBufferLength,
    int                 in_Argc,
    const   char              **in_ppArgv,
    uint8_t             in_DupEnv,
    const   char              **in_ppAdditionalEnv,
    uint8_t             in_ProxyActive,
    const   char               *in_Reserved,
    uint64_t            in_BufferSpace,
    const   char               *in_LibrarySearchPath,
    const   char               *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    COIPROCESS         *out_pProcess)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Engine);
        LOG_STR_PARAM(msg, in_pBinaryName);
        LOG_REG_PARAM(msg, in_pBinaryBuffer);
        LOG_REG_PARAM(msg, in_BinaryBufferLength);
        LOG_REG_PARAM(msg, in_Argc);
        LOG_REG_PARAM(msg, in_ppArgv);
        LOG_REG_PARAM(msg, (bool) in_DupEnv);
        LOG_REG_PARAM(msg, in_ppAdditionalEnv);
        LOG_REG_PARAM(msg, (bool) in_ProxyActive);
        LOG_STR_PARAM(msg, in_Reserved);
        LOG_REG_PARAM(msg, in_BufferSpace);
        LOG_STR_PARAM(msg, in_LibrarySearchPath);
        LOG_STR_PARAM(msg, in_FileOfOrigin);
        LOG_REG_PARAM(msg, in_FileOfOriginOffset);
        LOG_PTR_PARAM(msg, out_pProcess);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessCreateFromFile(
    COIENGINE           in_Engine,
    const   char               *in_pBinaryName,
    int                 in_Argc,
    const   char              **in_ppArgv,
    uint8_t             in_DupEnv,
    const   char              **in_ppAdditionalEnv,
    uint8_t             in_ProxyActive,
    const   char               *in_Reserved,
    uint64_t            in_BufferSpace,
    const   char               *in_LibrarySearchPath,
    COIPROCESS         *out_pProcess)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Engine);
        LOG_STR_PARAM(msg, in_pBinaryName);
        LOG_REG_PARAM(msg, in_Argc);
        LOG_REG_PARAM(msg, in_ppArgv);
        LOG_REG_PARAM(msg, (bool) in_DupEnv);
        LOG_REG_PARAM(msg, in_ppAdditionalEnv);
        LOG_REG_PARAM(msg, (bool) in_ProxyActive);
        LOG_STR_PARAM(msg, in_Reserved);
        LOG_REG_PARAM(msg, in_BufferSpace);
        LOG_STR_PARAM(msg, in_LibrarySearchPath);
        LOG_PTR_PARAM(msg, out_pProcess);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessDestroy(
    COIPROCESS              in_Process,
    int32_t                 in_WaitForMainTimeout,
    uint8_t                 in_ForceDestroy,
    int8_t                 *out_pProcessReturn,
    uint32_t               *out_pReason)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_WaitForMainTimeout);
        LOG_REG_PARAM(msg, (bool) in_ForceDestroy);
        LOG_PTR_PARAM(msg, out_pProcessReturn);
        LOG_PTR_PARAM(msg, out_pReason);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessGetFunctionHandles(
    COIPROCESS          in_Process,
    uint32_t            in_NumFunctions,
    const   char              **in_ppFunctionNameArray,
    COIFUNCTION        *out_pFunctionHandleArray)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_NumFunctions);
        LOG_ARY_PARAM(msg, in_ppFunctionNameArray, in_NumFunctions);
        LOG_ARY_PARAM(msg, out_pFunctionHandleArray, in_NumFunctions);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessLoadLibraryFromMemory(
    COIPROCESS          in_Process,
    const   void               *in_pLibraryBuffer,
    uint64_t            in_LibraryBufferLength,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    const   char               *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    COILIBRARY         *out_pLibrary)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_pLibraryBuffer);
        LOG_REG_PARAM(msg, in_LibraryBufferLength);
        LOG_STR_PARAM(msg, in_pLibraryName);
        LOG_STR_PARAM(msg, in_LibrarySearchPath);
        LOG_STR_PARAM(msg, in_FileOfOrigin);
        LOG_REG_PARAM(msg, in_FileOfOriginOffset);
        LOG_PTR_PARAM(msg, out_pLibrary);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessLoadLibraryFromMemory2(
    COIPROCESS          in_Process,
    const   void               *in_pLibraryBuffer,
    uint64_t            in_LibraryBufferLength,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    const   char               *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    uint32_t            in_Flags,
    COILIBRARY         *out_pLibrary)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_pLibraryBuffer);
        LOG_REG_PARAM(msg, in_LibraryBufferLength);
        LOG_STR_PARAM(msg, in_pLibraryName);
        LOG_STR_PARAM(msg, in_LibrarySearchPath);
        LOG_STR_PARAM(msg, in_FileOfOrigin);
        LOG_REG_PARAM(msg, in_FileOfOriginOffset);
        LOG_REG_PARAM(msg, in_Flags);
        LOG_PTR_PARAM(msg, out_pLibrary);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessLoadLibraryFromFile(
    COIPROCESS          in_Process,
    const   char               *in_pFileName,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    COILIBRARY         *out_pLibrary)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_STR_PARAM(msg, in_pFileName);
        LOG_STR_PARAM(msg, in_pLibraryName);
        LOG_STR_PARAM(msg, in_LibrarySearchPath);
        LOG_PTR_PARAM(msg, out_pLibrary);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessLoadLibraryFromFile2(
    COIPROCESS          in_Process,
    const   char               *in_pFileName,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    uint32_t            in_Flags,
    COILIBRARY         *out_pLibrary)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_STR_PARAM(msg, in_pFileName);
        LOG_STR_PARAM(msg, in_pLibraryName);
        LOG_STR_PARAM(msg, in_LibrarySearchPath);
        LOG_REG_PARAM(msg, in_Flags);
        LOG_PTR_PARAM(msg, out_pLibrary);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}


void
TRACE_COIProcessUnloadLibrary(
    COIPROCESS          in_Process,
    COILIBRARY          in_Library)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_Library);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessRegisterLibraries(
    uint32_t            in_NumLibraries,
    const   void              **in_ppLibraryArray,
    const   uint64_t           *in_pLibrarySizeArray,
    const   char              **in_ppFileOfOriginArray,
    const   uint64_t           *in_pFileOfOriginOffSetArray)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_NumLibraries);
        LOG_ARY_PARAM(msg, in_ppLibraryArray, in_NumLibraries);
        LOG_ARY_PARAM(msg, in_pLibrarySizeArray, in_NumLibraries);
        LOG_ARY_PARAM(msg, in_ppFileOfOriginArray, in_NumLibraries);
        LOG_ARY_PARAM(msg, in_pFileOfOriginOffSetArray, in_NumLibraries);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessSetCacheSize(
    const   COIPROCESS          in_Process,
    const   uint64_t            in_HugePagePoolSize,
    const   uint32_t            in_HugeFlags,
    const   uint64_t            in_SmallPagePoolSize,
    const   uint32_t            in_SmallFlags,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{

    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_HugePagePoolSize);
        LOG_REG_PARAM(msg, in_HugeFlags);
        LOG_REG_PARAM(msg, in_SmallPagePoolSize);
        LOG_REG_PARAM(msg, in_SmallFlags);
        LOG_REG_PARAM(msg, in_NumDependencies);
        LOG_ARY_PARAM(msg, in_pDependencies, in_NumDependencies);
        LOG_PTR_PARAM(msg, out_pCompletion);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIProcessConfigureDMA(
    const   uint64_t            in_Channels,
    const   COI_DMA_MODE        in_Mode)
{
    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Channels);
        LOG_REG_PARAM(msg, in_Mode);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIRegisterNotificationCallback(
    COIPROCESS                  in_Process,
    COI_NOTIFICATION_CALLBACK   in_Callback,
    const   void                       *in_UserData)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_Callback);
        LOG_REG_PARAM(msg, in_UserData);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COIUnregisterNotificationCallback(
    COIPROCESS                  in_Process,
    COI_NOTIFICATION_CALLBACK   in_Callback)
{
    if (!g_process) return;

    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_Process);
        LOG_REG_PARAM(msg, in_Callback);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}

void
TRACE_COINotificationCallbackSetContext(
    const   void                       *in_UserData)
{
    string coiFunction = GetCOIFunc();
    ostringstream msg;

    msg << coiFunction << " [ThID:" << (void *)pthread_self() << "]" << endl;
    if (!g_short)
    {
        LOG_REG_PARAM(msg, in_UserData);
    }
    fprintf(g_fPtr, "%s\n", msg.str().c_str());
}
