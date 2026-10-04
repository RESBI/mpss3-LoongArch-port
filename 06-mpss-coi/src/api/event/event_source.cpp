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

#include <source/COIEvent_source.h>
#include <common/COIMacros_common.h>

#include <internal/_DependencyDag.h>
#include <internal/_Log.h>
#include <internal/_Pipeline.h>
#include <internal/_Event.h>
#include <internal/coitrace.h>
#include <internal/coi_version_asm.h>

std::map<COIEVENT, _UserEvent *> _UserEventHandler::m_Events;
std::map<COIEVENT, uint32_t> _UserEventHandler::m_Canceled;
    pthread_mutex_t _UserEventHandler::m_lock = PTHREAD_MUTEX_INITIALIZER;


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIEventWait, 1)(
    uint16_t        in_NumEvents,
    const   COIEVENT       *in_pEvents,
    int32_t         in_Timeout,
    uint8_t         in_WaitForAll,
    uint32_t       *out_pNumSignaled,
    uint32_t       *out_pSignaledIndices)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_TIME_OUT_REACHED;

    // We could type check all the parameters here, but it seems
    // preferable to type check in the internal function, so we keep
    // the robustness for internal code as well. Otherwise we'd be
    // type checking twice, in order to get a minimal speed increase.
    coi_result = _COIEventWait(in_NumEvents, in_pEvents,
                               in_Timeout, in_WaitForAll,
                               out_pNumSignaled, out_pSignaledIndices);

    if (TRACE_COIEventWait)
    {
        TRACE_COIEventWait(in_NumEvents,
                           in_pEvents,
                           in_Timeout,
                           in_WaitForAll,
                           out_pNumSignaled,
                           out_pSignaledIndices);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT SYMBOL_VERSION(COIEventRegisterUserEvent, 1)(
    COIEVENT   *out_pEvent)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    if (!out_pEvent)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    {
        _UserEvent *node = new _UserEvent();

        //Add it to the event map
        TaskScheduler::Get().AddToMap(node);

        *out_pEvent = node->GetEvent();

        _UserEventHandler::Register(*out_pEvent, node);
    }
    coi_result = COI_SUCCESS;

end:

    if (TRACE_COIEventRegisterUserEvent)
    {
        TRACE_COIEventRegisterUserEvent(out_pEvent);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT SYMBOL_VERSION(COIEventUnregisterUserEvent, 1)(
    COIEVENT    in_Event)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    if (in_Event.opaque[1] != TaskNode::USER_EVENT)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    coi_result = TaskScheduler::Get().IsEventSignaled(in_Event);
    if (COI_RETRY == coi_result)
    {
        //First unregister the event which will mark it canceled.
        //Then wake up the threads so that cancel flag is not lost.
        TaskNode *n = TaskScheduler::Get().GetTaskNode(in_Event);
        _UserEventHandler::Unregister(in_Event);
        TaskScheduler::Get().Complete(n);
        TaskScheduler::Get().RunReady();
    }

    coi_result = COI_SUCCESS;
end:
    if (TRACE_COIEventUnregisterUserEvent)
    {
        TRACE_COIEventUnregisterUserEvent(in_Event);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT SYMBOL_VERSION(COIEventRegisterCallback, 1)(
    const   COIEVENT            in_Event,
    COI_EVENT_CALLBACK  in_Callback,
    const   void               *in_UserData,
    const   uint64_t            in_Flags)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    if (in_Event.opaque[1] == (uint64_t)TaskNode::INVALID_EVENT ||
            in_Event.opaque[1] > 2)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (in_Callback == NULL)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (in_Flags)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    coi_result = TaskScheduler::Get().IsEventSignaled(in_Event);
    if (COI_RETRY == coi_result)
    {
        //Event is not signaled yet, mark tasknode with appropriate
        //handlers to do callback later
        coi_result = TaskScheduler::Get().SetTaskNodeCallBackData(
                         in_Event,
                         in_Callback,
                         in_UserData);
        if (coi_result != COI_SUCCESS)
        {
            //The event finished before we were able to set the data.
            //So we need to get the real return value and do the callback.
            coi_result = TaskScheduler::Get().IsEventSignaled(in_Event);
            in_Callback(in_Event, coi_result, in_UserData);
        }
    }
    else
    {
        //The event is done and not pending. This means
        //it was COI_SUCCESS or the error code.
        in_Callback(in_Event, coi_result, in_UserData);
    }
    coi_result = COI_SUCCESS;
end:
    if (TRACE_COIEventRegisterCallback)
    {
        TRACE_COIEventRegisterCallback(
            in_Event,
            in_Callback,
            in_UserData,
            in_Flags);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

#ifdef __cplusplus
}
#endif // __cplusplus
