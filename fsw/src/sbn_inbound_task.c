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
#include "sbn_resource.h"
#include "sbn_connection.h"
//jphfix #include "cfe_es.h" /* PerfLog */
//jphfix #include "cfe_platform_cfg.h"

static void SBN_Inbound_AddPeerDataSchedule(SBN_PeerRecord_t *PeerRec)
{
    SBN_WorkerTask_WorkItem_t WorkItem;

    memset(&WorkItem, 0, sizeof(WorkItem));

    WorkItem.Info.Comms.NetId = PeerRec->NetId;
    WorkItem.Info.Comms.PeerId = SBN_PeerRecordGetID(PeerRec);
    WorkItem.JobType = SBN_WorkerTask_JobType_PEER_INBOUND;

    PeerRec->State.InboundDataIsScheduled = SBN_WorkerTask_ScheduleJob(&WorkItem);
}

static void SBN_Inbound_AddNetDataSchedule(SBN_NetRecord_t *NetRec)
{
    SBN_WorkerTask_WorkItem_t WorkItem;

    memset(&WorkItem, 0, sizeof(WorkItem));

    WorkItem.Info.Comms.NetId = SBN_NetRecordGetID(NetRec);
    WorkItem.JobType = SBN_WorkerTask_JobType_NET_INBOUND;

    NetRec->State.InboundDataIsScheduled = SBN_WorkerTask_ScheduleJob(&WorkItem);
}

void SBN_InboundPeerDataJob(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    SBN_Status_t Status;
    SBN_PeerInterface_t *Peer;
    SBN_NetInterface_t *Net;
    bool IsOK;
    int32 MsgLim;

    Peer = &PeerRec->Intf;
    Net = Peer->Net;
    IsOK = true;
    MsgLim = 100; /* jphfix - make platform config */

    EVSSendDbg(SBN_PEERTASK_EID, "Try to receive from peer...");

    while(MsgLim > 0 && IsOK)
    {
        --MsgLim;

        Status = Net->IfOps->RecvFromPeer(Net, Peer, &D->MsgType, &D->ActualDataSz, &D->ProcessorID, &D->SpacecraftID, D->DataPtr);
        if (Status != SBN_SUCCESS)
        {
            /* This is the acceptable status, anything else is a problem */
            IsOK = (Status == SBN_IF_EMPTY);
            break;
        }

        SBN_ProcessPeerMsg(PeerRec, D);
    }

    if (PeerRec->State.InboundAutoReschedule && IsOK)
    {
        /* In task mode, immediately reschedule this same job */
        SBN_Inbound_AddPeerDataSchedule(PeerRec);
    }
    else
    {
        /* it will be scheduled again at the next cycle */
        PeerRec->State.InboundDataIsScheduled = false;
        if (!IsOK)
        {
            EVSSendErr(SBN_PEERTASK_EID,
                        "%s failed for peer %d: status=%d",
                        __func__,
                        SBN_PeerId_AsInt(SBN_PeerRecordGetID(PeerRec)),
                        (int)Status);
        }
    }
}

/**
 * \brief Receive task created for each net-based connection.
 * Spanwed from SBN_PeerPoll()
 */
void SBN_InboundNetDataJob(SBN_NetRecord_t *NetRec, SBN_BufferDesc_t *D)
{
    SBN_Status_t Status;
    SBN_PeerRecord_t *PeerRec;
    SBN_NetInterface_t *Net;
    bool IsOK;
    int32 MsgLim;

    Net = &NetRec->Intf;
    IsOK = true;
    MsgLim = 100; /* jphfix - make platform config */

    EVSSendDbg(SBN_PEERTASK_EID, "Try to receive from net...");

    while(MsgLim > 0 && IsOK)
    {
        --MsgLim;

        Status = Net->IfOps->RecvFromNet(Net, &D->MsgType, &D->ActualDataSz, &D->ProcessorID, &D->SpacecraftID, D->DataPtr);
        if (Status != SBN_SUCCESS)
        {
            /* This is the acceptable status, anything else is a problem */
            IsOK = (Status == SBN_IF_EMPTY);
            break;
        }

        PeerRec = SBN_GetPeerRecord(NetRec, D->ProcessorID, D->SpacecraftID);
        if (PeerRec == NULL)
        {
            EVSSendErr(SBN_PEERTASK_EID, "unknown peer (ProcessorID=%d)", (int)D->ProcessorID);
        }
        else
        {
            SBN_ProcessPeerMsg(PeerRec, D);
        }
    }

    if (NetRec->State.InboundAutoReschedule && IsOK)
    {
        /* In task mode, immediately reschedule this same job */
        SBN_Inbound_AddNetDataSchedule(NetRec);
    }
    else
    {
        /* it will be scheduled again at the next cycle */
        NetRec->State.InboundDataIsScheduled = false;

        /* for informational purposes, something went wrong */
        if (!IsOK)
        {
            EVSSendErr(SBN_PEERTASK_EID,
                        "%s failed for net %d: status=%d",
                        __func__,
                        SBN_NetId_AsInt(SBN_NetRecordGetID(NetRec)),
                        (int)Status);
        }
    }
}

/*
 * SBN_InboundPollCycle()
 */
SBN_Status_t SBN_DoInboundNetPollCycle(SBN_NetRecord_t* NetRec, void *Arg)
{
    if (NetRec->State.InboundSchedulingEnabled && !NetRec->State.InboundDataIsScheduled)
    {
        SBN_Inbound_AddNetDataSchedule(NetRec);
    }

    return SBN_SUCCESS;
}

SBN_Status_t SBN_DoInboundPeerPollCycle(SBN_PeerRecord_t* PeerRec, void *Arg)
{
    if (PeerRec->State.InboundSchedulingEnabled && !PeerRec->State.InboundDataIsScheduled && SBN_Connection_IsPeerReady(PeerRec))
    {
        SBN_Inbound_AddPeerDataSchedule(PeerRec);
    }

    return SBN_SUCCESS;
}

void SBN_InboundPollCycle(void)
{
    SBN_ForEachConfiguredNet(SBN_DoInboundNetPollCycle, NULL);
    SBN_ForEachConfiguredPeer(SBN_DoInboundPeerPollCycle, NULL);
}
