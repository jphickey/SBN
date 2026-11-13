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
#include "sbn_connection.h"
#include "sbn_worker_task.h"
#include "sbn_outbound_task.h"
#include "sbn_filter.h"

#define SBN_CONNECTION_CHECK_TIMER   10
#define SBN_CONNECTION_CHECK_PENDING 0xFF

bool SBN_Connection_IsPeerReady(SBN_PeerRecord_t *PeerRec)
{
    /* Notably - the pipe management and subscription sync must be done before we pass data, but the connection state
     * boolean is set by worker threads.  The pipe state lags behind the connection state
     * since the main thread reacts to changes indicated.  The peer can only accept outbound
     * data when _both_ are true. */
    return (PeerRec->Intf.Connected && CFE_RESOURCEID_TEST_DEFINED(PeerRec->Intf.Pipe) &&
        PeerRec->PeerState.LocalPipeSubVersion == PeerRec->PeerState.Q.CurrVersion);
}

static void SBN_Outbound_AddCheckSchedule(SBN_PeerRecord_t *PeerRec)
{
    SBN_WorkerTask_WorkItem_t WorkItem;

    memset(&WorkItem, 0, sizeof(WorkItem));

    WorkItem.JobType = SBN_WorkerTask_JobType_CHECK_CONNECTION;
    WorkItem.Info.Comms.PeerId = SBN_PeerRecordGetID(PeerRec);
    WorkItem.Info.Comms.NetId = PeerRec->NetId;

    if (SBN_WorkerTask_ScheduleJob(&WorkItem))
    {
        PeerRec->PeerState.ConnectionCheckCounter = SBN_CONNECTION_CHECK_PENDING;
    }
}

void SBN_Connection_HandleDisconnect(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    static const char FAIL_PREFIX[] = "ERROR: could not disconnect peer:";
    CFE_Status_t      CFE_Status;

    if (CFE_RESOURCEID_TEST_DEFINED(PeerRec->Intf.Pipe))
    {
        CFE_Status = CFE_SB_DeletePipe(PeerRec->Intf.Pipe);
        PeerRec->Intf.Pipe = CFE_SB_INVALID_PIPE;
    }
    else
    {
        CFE_Status = CFE_SUCCESS;
    }

    if (CFE_Status != CFE_SUCCESS)
    {
        EVSSendErr(SBN_PEER_EID,
                   "%s could not delete pipe when disconnecting peer %d:%d: 0x%08x",
                   FAIL_PREFIX,
                   (int)PeerRec->Intf.SpacecraftID,
                   (int)PeerRec->Intf.ProcessorID,
                   (unsigned int)CFE_Status);

    }

    PeerRec->PeerState.LocalPipeSubCnt = 0;
    PeerRec->Intf.SubCnt = 0;

    EVSSendInfo(SBN_PEER_EID, "Disconnected from peer %d:%d.",
                   (int)PeerRec->Intf.SpacecraftID,
                   (int)PeerRec->Intf.ProcessorID);
}

SBN_Status_t SBN_Connection_SendProtocolVersionMsg(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    SBN_Status_t      SBN_Status;
    uint8            *ProtocolVer;

    ProtocolVer = D->DataPtr;
    D->ActualDataSz = sizeof(*ProtocolVer);
    D->MsgType = SBN_PROTO_MSG;

    *ProtocolVer = SBN_PROTO_VER;

    SBN_Status  = SBN_Outbound_SendToPeer(&PeerRec->Intf, D);

    return SBN_Status;
}

void SBN_Connection_HandleConnect(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    static const char FAIL_PREFIX[] = "ERROR: could not disconnect peer:";
    CFE_Status_t      CFE_Status;
    char              PipeName[OS_MAX_API_NAME];

    if (!CFE_RESOURCEID_TEST_DEFINED(PeerRec->Intf.Pipe))
    {
        /* create a pipe name string similar to SBN_0_Pipe */
        snprintf(PipeName, OS_MAX_API_NAME, "SBN_%d_%d_Pipe",
                    (int)PeerRec->Intf.SpacecraftID,
                    (int)PeerRec->Intf.ProcessorID);

        CFE_Status = CFE_SB_CreatePipe(&PeerRec->Intf.Pipe, SBN_PLATFORM_PEER_PIPE_DEPTH, PipeName);

        if (CFE_Status != CFE_SUCCESS)
        {
            EVSSendErr(SBN_PEER_EID, "%s: could not create peer pipe '%s'", FAIL_PREFIX, PipeName);
        }
        else
        {
            EVSSendInfo(SBN_PEER_EID, "Created peer pipe '%s'", PipeName);

            CFE_Status = CFE_SB_SetPipeOpts(PeerRec->Intf.Pipe, CFE_SB_PIPEOPTS_IGNOREMINE);
            if (CFE_Status != CFE_SUCCESS)
            {
                EVSSendErr(SBN_PEER_EID, "%s: could not set pipe options '%s'", FAIL_PREFIX, PipeName);
            }

            /* Reset the remote subscription info - peer must re-send it */
            PeerRec->PeerState.LocalPipeSubCnt = 0;
        }
    }

    /* force a full refresh of the subscriptions at the next check */
    PeerRec->PeerState.PeerSentSubVersion = SBN_AppData.SubChangeQ.CurrVersion - SBN_SUBSCRIPTION_UPDATE_QUEUE_DEPTH;

    SBN_Connection_SendProtocolVersionMsg(PeerRec, D);
}

void SBN_CheckPeerConnectionJob(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    SBN_Status_t         SBN_Status;
    SBN_PeerInterface_t *Peer;
    const SBN_IfOps_t *IfOps;

    Peer = &PeerRec->Intf;
    IfOps = Peer->Net->IfOps;

    // Poll peer here to detect disconnections and to reconnect
    SBN_Status = IfOps->PollPeer(Peer);

    if (SBN_Status != SBN_SUCCESS)
    {
        EVSSendErr(SBN_PEERTASK_EID, "failed to poll peer %d:%d", Peer->SpacecraftID, Peer->ProcessorID);
    }

    /* important: set this so it will be rescheduled again */
    PeerRec->PeerState.ConnectionCheckCounter = SBN_CONNECTION_CHECK_TIMER;
}

static SBN_Status_t SBN_Connection_DoPollCycle(SBN_PeerRecord_t *PeerRec, void *Arg)
{
    bool IsLocalConnected;
    SBN_BufferDesc_t Desc;

    memset(&Desc, 0, sizeof(Desc));
    Desc.MaxDataSz = sizeof(SBN_AppData.TempMsg);
    Desc.DataPtr = SBN_AppData.TempMsg;
    Desc.ProcessorID = CFE_PSP_GetProcessorId();
    Desc.SpacecraftID = CFE_PSP_GetSpacecraftId();

    /* The existence of a local pipe for this peer indicates the connection
    * has been locally established. */
    IsLocalConnected = CFE_RESOURCEID_TEST_DEFINED(PeerRec->Intf.Pipe);

    /* The actual work is done by helper threads, just schedule it. */
    if (PeerRec->PeerState.ConnectionCheckCounter != SBN_CONNECTION_CHECK_PENDING)
    {
        if (PeerRec->PeerState.ConnectionCheckCounter > 0)
        {
            if (IsLocalConnected == PeerRec->Intf.Connected)
            {
                /* State not changed, so poll normally */
                --PeerRec->PeerState.ConnectionCheckCounter;
            }
            else
            {
                /* something changed so schedule it ASAP */
                PeerRec->PeerState.ConnectionCheckCounter = 0;
            }
        }
        if (PeerRec->PeerState.ConnectionCheckCounter == 0)
        {
            SBN_Outbound_AddCheckSchedule(PeerRec);
        }
    }

    if (IsLocalConnected != PeerRec->Intf.Connected)
    {
        /* Something changed -- either a connect or disconnect */
        if (IsLocalConnected)
        {
            SBN_Connection_HandleDisconnect(PeerRec, &Desc);
            IsLocalConnected = false;
        }
        else
        {
            SBN_Connection_HandleConnect(PeerRec, &Desc);
        }
    }

    if (IsLocalConnected)
    {
        SBN_Subs_UpdatePeer(PeerRec, &Desc);
    }

    return SBN_SUCCESS;
}

void SBN_Connection_PollCycle(void)
{
    /* this schedules the sending of heartbeats (if needed) and health-checking */
    SBN_ForEachConfiguredPeer(SBN_Connection_DoPollCycle, NULL);
}
