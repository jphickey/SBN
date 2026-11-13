/************************************************************************
 * NASA Docket No. GSC-19,200-1, and identified as "cFS Draco"
 *
 * Copyright (c) 2023 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/*
 ** Include Files
 */
#include <string.h>

#include "sbn_error.h"
#include "sbn_app.h"
#include "sbn_worker_task.h"
#include "sbn_outbound_task.h"
#include "sbn_filter.h"
#include "sbn_connection.h"

/**
 * Sends a message to a peer using the module's send API.
 *
 * @param MsgType SBN type of the message
 * @param MsgSz Size of the message
 * @param Msg Message to send
 * @param Peer The peer to send the message to.
 * @return Number of characters sent on success, -1 on error.
 *
 */
SBN_Status_t SBN_Outbound_SendToPeer(SBN_PeerInterface_t *Peer, SBN_BufferDesc_t *D)
{
    SBN_NetInterface_t *Net;
    SBN_Status_t        SBN_Status;

    Net = Peer->Net;

    SBN_Status = Net->IfOps->Send(Peer, D->MsgType, D->ActualDataSz, D->DataPtr);

    if (SBN_Status != SBN_SUCCESS)
    {
        Peer->Counts.SendErrCnt++;
    }
    else
    {
        Peer->Counts.SendCnt++;
    }

    /* for clients that need a poll or heartbeat, update time even when failing */
    OS_GetLocalTime(&Peer->LastSend);

    /* Reset the buffer because this message was sent */
    D->MsgType = SBN_NO_MSG;
    D->ActualDataSz = 0;

    return SBN_Status;
} /* end SBN_SendNetMsg */

static void SBN_Outbound_AddDataSchedule(SBN_PeerRecord_t *PeerRec)
{
    SBN_WorkerTask_WorkItem_t WorkItem;

    memset(&WorkItem, 0, sizeof(WorkItem));

    WorkItem.JobType = SBN_WorkerTask_JobType_PEER_OUTBOUND;
    WorkItem.Info.Comms.PeerId = SBN_PeerRecordGetID(PeerRec);
    WorkItem.Info.Comms.NetId = PeerRec->NetId;

    PeerRec->State.OutboundDataIsScheduled = SBN_WorkerTask_ScheduleJob(&WorkItem);
}

void SBN_OutboundPeerDataJob(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    const CFE_SB_Buffer_t *BufPtr;
    int32 TimeOut;
    int32 MsgLim;
    CFE_MSG_Size_t MsgSz;
    CFE_SB_MsgId_t MsgId;
    CFE_Status_t CFE_Status;
    SBN_Status_t SBN_Status;
    bool IsOK;

    IsOK = SBN_Connection_IsPeerReady(PeerRec);
    BufPtr = NULL;
    MsgLim = 100; /* jphfix - make platform config */

    if (PeerRec->State.InboundAutoReschedule)
    {
        /* Emulate a dedicated task by pending on the pipe, but not forever */
        TimeOut = 100;
    }
    else
    {
        /* A normal Poll */
        TimeOut = CFE_SB_POLL;
    }

    while(MsgLim > 0 && IsOK)
    {
        --MsgLim;

        CFE_Status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&BufPtr, PeerRec->Intf.Pipe, TimeOut);
        if (CFE_Status != CFE_SUCCESS)
        {
            /* These two status codes are acceptable, anything else is a problem */
            IsOK = (CFE_Status == CFE_SB_TIME_OUT || CFE_Status == CFE_SB_NO_MESSAGE);
            break;
        }

        if (CFE_MSG_GetSize(&BufPtr->Msg, &MsgSz) != CFE_SUCCESS)
        {
            /* this is not supposed to happen */
            IsOK = false;
        }
        else if (MsgSz <= D->MaxDataSz)
        {
            /* We must make a copy of the message before modifying it.
             * JPHFIX: this is highly inefficient - replace with a useful copy */
            memcpy(D->DataPtr, &BufPtr->Msg, MsgSz);
            D->ActualDataSz = MsgSz;
            D->MsgType = SBN_APP_MSG;

            SBN_Status = SBN_Filter_ProcessModifiers(PeerRec, D);
            if (SBN_Status == SBN_SUCCESS)
            {
                CFE_MSG_GetMsgId(&BufPtr->Msg, &MsgId);
                OS_printf("%s(): FORWARDING --> 0x%04x %u\n",__func__, CFE_SB_MsgIdToValue(MsgId), (unsigned int)MsgSz);
                SBN_Status = SBN_Outbound_SendToPeer(&PeerRec->Intf, D);

                /* The connection might have been broken, stop trying to send if this fails */
                IsOK =  (SBN_Status == SBN_SUCCESS);
            }
        }
    }

    if (PeerRec->State.OutboundAutoReschedule && IsOK)
    {
        /* In task mode, immediately reschedule this same job */
        SBN_Outbound_AddDataSchedule(PeerRec);
    }
    else
    {
        /* it will be scheduled again at the next cycle */
        PeerRec->State.OutboundDataIsScheduled = false;
    }
}

static SBN_Status_t SBN_DoOutboundPollCycle(SBN_PeerRecord_t *PeerRec, void *Arg)
{
    /* The actual work is done by helper threads, just schedule it. */
    if (SBN_Connection_IsPeerReady(PeerRec) && !PeerRec->State.OutboundDataIsScheduled)
    {
        SBN_Outbound_AddDataSchedule(PeerRec);
    }

    return SBN_SUCCESS;
}

void SBN_OutboundPollCycle(void)
{
    SBN_ForEachConfiguredPeer(SBN_DoOutboundPollCycle, NULL);
}
