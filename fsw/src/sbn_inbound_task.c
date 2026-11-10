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
//jphfix #include "cfe_es.h" /* PerfLog */
//jphfix #include "cfe_platform_cfg.h"

void SBN_RecvPeerJob(SBN_RecvPeerTaskData_t *D)
{
    D->Status =
        D->Net->IfOps
            ->RecvFromPeer(D->Net, D->Peer, &D->MsgType, &D->MsgSz, &D->ProcessorID, &D->SpacecraftID, &D->NetMsgBuf);

    if (D->Status == SBN_SUCCESS)
    {
        OS_GetLocalTime(&D->Peer->LastRecv);

        D->Status = SBN_ProcessNetMsg(D->Net, D->MsgType, D->ProcessorID, D->SpacecraftID, D->MsgSz, &D->NetMsgBuf);
    }
    else
    {
        EVSSendErr(SBN_PEER_EID, "recv error (%d)", D->Status);
        D->Peer->Counts.RecvErrCnt++;
        return;
    } /* end if */
}

/**
 * \brief Receive task created for each net-based connection.
 * Spanwed from SBN_PeerPoll()
 */
void SBN_RecvNetJob(SBN_RecvNetTaskData_t *D)
{
    EVSSendDbg(SBN_PEERTASK_EID, "Try to receive from net...");
    D->Status =
        D->Net->IfOps->RecvFromNet(D->Net, &D->MsgType, &D->MsgSz, &D->ProcessorID, &D->SpacecraftID, &D->NetMsgBuf);

    if (D->Status != SBN_SUCCESS)
    {
        EVSSendErr(SBN_PEERTASK_EID,
                    "%s failed for net %d: status=0x%08X",
                    __func__,
                    D->NetIdx,
                    D->Status);
    }
    else
    {
        D->Peer = SBN_GetPeer(D->Net, D->ProcessorID, D->SpacecraftID);
        if (D->Peer == NULL)
        {
            EVSSendErr(SBN_PEERTASK_EID, "unknown peer (ProcessorID=%d)", (int)D->ProcessorID);
        }
        else
        {
            OS_GetLocalTime(&D->Peer->LastRecv);

            D->Status = SBN_ProcessNetMsg(D->Net, D->MsgType, D->ProcessorID, D->SpacecraftID, D->MsgSz, &D->NetMsgBuf);

            if (D->Status != SBN_SUCCESS)
            {
                EVSSendErr(SBN_PEERTASK_EID, "SBN_ProcessNetMsg failed: 0x%08X", D->Status);
            }
        }
    }
}


SBN_PeerContainer_t *SBN_Peer_GetContainer(SBN_PeerInterface_t *PeerIntf)
{
    const size_t Offset = offsetof(SBN_PeerContainer_t, Intf);

    return (void*)((char *)PeerIntf - Offset);
}


/*
 * SBN_InboundPollCycle()
 */
void SBN_InboundPollCycle(void)
{
    SBN_PeerContainer_t *Peer;
    SBN_NetContainer_t  *Net;
    SBN_NetIdx_t         NetIdx;
    SBN_PeerIdx_t        PeerIdx;
    SBN_WorkerTask_WorkItem_t WorkItem;

    for (NetIdx = 0; NetIdx < SBN_AppData.NetCnt; NetIdx++)
    {
        Net = &SBN_AppData.NetContainer[NetIdx];

        if (Net->Intf.IfOps->RecvFromNet && !Net->NetPoll.InboundScheduled)
        {
            memset(&WorkItem, 0, sizeof(WorkItem));

            WorkItem.JobType = SBN_WorkerTask_JobType_NET_INBOUND;
            WorkItem.NetIdx = NetIdx;

            Net->NetPoll.InboundScheduled = SBN_WorkerTask_ScheduleJob(&WorkItem);
        }
        else if (Net->Intf.IfOps->RecvFromPeer)
        {
            for (PeerIdx = 0; PeerIdx < Net->Intf.PeerCnt; PeerIdx++)
            {
                Peer = SBN_Peer_GetContainer(Net->Intf.PeerRef[PeerIdx]);

                if (!Peer->PeerPoll.InboundScheduled)
                {
                    memset(&WorkItem, 0, sizeof(WorkItem));

                    WorkItem.JobType = SBN_WorkerTask_JobType_PEER_INBOUND;
                    WorkItem.NetIdx = NetIdx;
                    WorkItem.PeerIdx = PeerIdx;

                    Peer->PeerPoll.InboundScheduled = SBN_WorkerTask_ScheduleJob(&WorkItem);
                }
            }
        }
    }
}

#ifdef jphfix
/**
 * \brief Receive task created for each direct peer-based connection.
 * Spanwed from SBN_PeerPoll()
 */
void SBN_RecvPeerTask(void)
{
    SBN_RecvPeerTaskData_t D;
    memset(&D, 0, sizeof(D));

    CFE_ES_GetTaskID(&D.RecvTaskID);

    for (D.NetIdx = 0; D.NetIdx < SBN_AppData.NetCnt; D.NetIdx++)
    {
        D.Net = &SBN_AppData.Nets[D.NetIdx];
        if (!D.Net->Configured)
        {
            continue;
        }

        for (D.PeerIdx = 0; D.PeerIdx < D.Net->PeerCnt; D.PeerIdx++)
        {
            D.Peer = &D.Net->Peers[D.PeerIdx];
            if (CFE_RESOURCEID_TEST_EQUAL(D.Peer->RecvTaskID, D.RecvTaskID))
            {
                break;
            } /* end if */
        } /* end for */

        if (D.PeerIdx < D.Net->PeerCnt)
        {
            break;
        } /* end if */
    } /* end for */

    if (D.NetIdx == SBN_AppData.NetCnt)
    {
        EVSSendErr(SBN_PEERTASK_EID, "unable to connect task to peer struct");
        return;
    } /* end if */

    while (1)
    {
        D.Status =
            D.Net->IfOps
                ->RecvFromPeer(D.Net, D.Peer, &D.MsgType, &D.MsgSz, &D.ProcessorID, &D.SpacecraftID, &D.NetMsgBuf);

        if (D.Status == SBN_IF_EMPTY)
        {
            continue; /* no (more) messages */
        } /* end if */

        if (D.Status == SBN_SUCCESS)
        {
            OS_GetLocalTime(&D.Peer->LastRecv);

            D.Status = SBN_ProcessNetMsg(D.Net, D.MsgType, D.ProcessorID, D.SpacecraftID, D.MsgSz, &D.NetMsgBuf);

            if (D.Status != SBN_SUCCESS)
            {
                D.Peer->RecvTaskID = CFE_ES_TASKID_UNDEFINED;
                return;
            } /* end if */
        }
        else
        {
            EVSSendErr(SBN_PEER_EID, "recv error (%d)", D.Status);
            D.Peer->RecvErrCnt++;
            D.Peer->RecvTaskID = CFE_ES_TASKID_UNDEFINED;
            return;
        } /* end if */
    } /* end while */
} /* end SBN_RecvPeerTask() */

/**
 * \brief Receive task created for each net-based connection.
 * Spanwed from SBN_PeerPoll()
 */
void SBN_RecvNetTask(void)
{
    static const char     FAIL_PREFIX_STARTUP[] = "ERROR: could not start SBN Receive Net Task:";
    static const char     FAIL_PREFIX_RUNNING[] = "ERROR: during SBN Receive Net Task:";
    SBN_RecvNetTaskData_t D;
    memset(&D, 0, sizeof(D));

    CFE_ES_GetTaskID(&D.RecvTaskID);

    for (D.NetIdx = 0; D.NetIdx < SBN_AppData.NetCnt; D.NetIdx++)
    {
        D.Net = &SBN_AppData.Nets[D.NetIdx];
        if (CFE_RESOURCEID_TEST_EQUAL(D.Net->RecvTaskID, D.RecvTaskID))
        {
            break;
        } /* end if */
    } /* end for */

    if (D.NetIdx == SBN_AppData.NetCnt)
    {
        EVSSendErr(SBN_PEERTASK_EID, "%s unable to connect task to net struct", FAIL_PREFIX_STARTUP);
        return;
    } /* end if */

    while (1)
    {
        EVSSendDbg(SBN_PEERTASK_EID, "Try to receive from net...");
        D.Status =
            D.Net->IfOps->RecvFromNet(D.Net, &D.MsgType, &D.MsgSz, &D.ProcessorID, &D.SpacecraftID, &D.NetMsgBuf);

        if (D.Status == SBN_IF_EMPTY)
        {
            continue; /* no (more) messages */
        } /* end if */

        if (D.Status != SBN_SUCCESS)
        {
            EVSSendErr(SBN_PEERTASK_EID,
                       "%s RecvFromNet failed for net %d: status=0x%08X",
                       FAIL_PREFIX_RUNNING,
                       D.NetIdx,
                       D.Status);
            break;
        } /* end if */

        D.Peer = SBN_GetPeer(D.Net, D.ProcessorID, D.SpacecraftID);
        if (!D.Peer)
        {
            EVSSendErr(SBN_PEERTASK_EID, "unknown peer (ProcessorID=%d)", (int)D.ProcessorID);
            break;
        } /* end if */

        OS_GetLocalTime(&D.Peer->LastRecv);

        D.Status = SBN_ProcessNetMsg(D.Net, D.MsgType, D.ProcessorID, D.SpacecraftID, D.MsgSz, &D.NetMsgBuf);

        if (D.Status != SBN_SUCCESS)
        {
            EVSSendErr(SBN_PEERTASK_EID, "SBN_ProcessNetMsg failed: 0x%08X", D.Status);
            break;
        } /* end if */
    } /* end while */

    /* Unset the task id so that it can be created if necessary */
    D.Net->RecvTaskID = CFE_ES_TASKID_UNDEFINED;
} /* end SBN_RecvNetTask() */
#endif

#ifdef jphfix // this is the polled version of the task
/**
 * Checks all interfaces for messages from peers.
 * Receive messages from the specified peer, injecting them onto the local
 * software bus.
 */
SBN_Status_t SBN_RecvNetMsgs(void)
{
    SBN_Status_t         SBN_Status;
    SBN_PeerInterface_t *Peer;
    uint8                MsgCnt;
    SBN_NetIdx_t         NetIdx;
    SBN_NetInterface_t  *Net;
    SBN_MsgType_t        MsgType;
    SBN_MsgSz_t          MsgSz;
    SBN_ProcessorID_t    ProcessorID;
    SBN_SpacecraftID_t   SpacecraftID;
    SBN_PeerIdx_t        PeerIdx;

    for (NetIdx = 0; NetIdx < SBN_AppData.NetCnt; NetIdx++)
    {
        Net = &SBN_AppData.Nets[NetIdx];
        if (Net->TaskFlags & SBN_TASK_RECV)
        {
            continue; /* separate task handles receiving from a net */
        } /* end if */

        if (Net->IfOps->RecvFromNet)
        {
            // TODO: make configurable
            for (MsgCnt = 0; MsgCnt < 100; MsgCnt++) /* read at most 100 messages from the net */
            {
                /*memset(SBN_AppData.MsgBuffer, 0, sizeof(SBN_AppData.MsgBuffer));*/

                SBN_Status =
                    Net->IfOps->RecvFromNet(Net, &MsgType, &MsgSz, &ProcessorID, &SpacecraftID, SBN_AppData.MsgBuffer);

                if (SBN_Status == SBN_IF_EMPTY)
                {
                    break; /* no (more) messages for this net, continue to next net */
                } /* end if */

                /* for UDP, the message received may not be from the peer
                 * expected.
                 */
                Peer = SBN_GetPeer(Net, ProcessorID, SpacecraftID);

                if (!Peer)
                {
                    EVSSendInfo(SBN_PEERTASK_EID, "unknown peer (ProcessorID=%d)", (int)ProcessorID);
                    /* may be a misconfiguration on my part...? continue processing msgs... */
                    continue;
                } /* end if */

                OS_GetLocalTime(&Peer->LastRecv);
                SBN_ProcessNetMsg(Net,
                                  MsgType,
                                  ProcessorID,
                                  SpacecraftID,
                                  MsgSz,
                                  SBN_AppData.MsgBuffer); /* ignore errors */
            } /* end for */
        }
        else if (Net->IfOps->RecvFromPeer)
        {
            for (PeerIdx = 0; PeerIdx < Net->PeerCnt; PeerIdx++)
            {
                Peer = &Net->Peers[PeerIdx];

                // TODO: make configurable
                for (MsgCnt = 0; MsgCnt < 100; MsgCnt++) /* read at most 100 messages from peer */
                {
                    memset(SBN_AppData.MsgBuffer, 0, sizeof(SBN_AppData.MsgBuffer));

                    SBN_Status = Net->IfOps->RecvFromPeer(Net,
                                                          Peer,
                                                          &MsgType,
                                                          &MsgSz,
                                                          &ProcessorID,
                                                          &SpacecraftID,
                                                          SBN_AppData.MsgBuffer);

                    if (SBN_Status == SBN_IF_EMPTY)
                    {
                        break; /* no (more) messages for this peer, continue to next peer */
                    } /* end if */

                    OS_GetLocalTime(&Peer->LastRecv);

                    SBN_Status =
                        SBN_ProcessNetMsg(Net, MsgType, ProcessorID, SpacecraftID, MsgSz, SBN_AppData.MsgBuffer);

                    if (SBN_Status != SBN_SUCCESS)
                    {
                        break; /* continue to next peer */
                    } /* end if */
                } /* end for */
            } /* end for */
        }
        else
        {
            EVSSendErr(SBN_PEER_EID, "neither RecvFromPeer nor RecvFromNet defined for net #%d", (int)NetIdx);

            /* meanwhile, continue to next net... */
        } /* end if */
    } /* end for */

    return SBN_SUCCESS;
} /* end SBN_RecvNetMsgs */
#endif

#ifdef jphfix
/**
 * Iterate through all nets and create receive tasks if they do not yet exist.
 */
static SBN_Status_t SBN_PeerPoll(void)
{
    CFE_Status_t         CFE_Status;
    SBN_NetIdx_t         NetIdx;
    SBN_NetInterface_t  *Net;
    char                 RecvTaskName[32];
    SBN_PeerIdx_t        PeerIdx;
    SBN_PeerInterface_t *Peer;

    for (NetIdx = 0; NetIdx < SBN_AppData.NetCnt; NetIdx++)
    {
        Net = &SBN_AppData.Nets[NetIdx];

        if (Net->IfOps->RecvFromNet && Net->TaskFlags & SBN_TASK_RECV)
        {
            if (!CFE_RESOURCEID_TEST_DEFINED(Net->RecvTaskID))
            {
                EVSSendInfo(SBN_PEER_EID, "Creating recv task for net %d", (int)NetIdx);

                /* TODO: add logic/controls to prevent hammering */
                snprintf(RecvTaskName, OS_MAX_API_NAME, "sbn_rs_%d", (int)NetIdx);
                CFE_Status =
                    CFE_ES_CreateChildTask(&(Net->RecvTaskID),
                                           RecvTaskName,
                                           (CFE_ES_ChildTaskMainFuncPtr_t)&SBN_RecvNetTask,
                                           NULL,
                                           CFE_PLATFORM_ES_DEFAULT_STACK_SIZE + 2 * sizeof(SBN_RecvNetTaskData_t),
                                           0,
                                           0);

                if (CFE_Status != CFE_SUCCESS)
                {
                    EVSSendErr(SBN_PEER_EID, "error creating task for net %d", (int)NetIdx);
                    return SBN_ERROR;
                } /* end if */
            } /* end if */
        }
        else
        {
            for (PeerIdx = 0; PeerIdx < Net->PeerCnt; PeerIdx++)
            {
                Peer = &Net->Peers[PeerIdx];

                if (Net->IfOps->RecvFromPeer && Peer->TaskFlags & SBN_TASK_RECV)
                {
                    if (!CFE_RESOURCEID_TEST_DEFINED(Peer->RecvTaskID))
                    {
                        /* TODO: add logic/controls to prevent hammering */
                        snprintf(RecvTaskName, OS_MAX_API_NAME, "sbn_recv_%d", (int)PeerIdx);
                        CFE_Status = CFE_ES_CreateChildTask(&(Peer->RecvTaskID),
                                                            RecvTaskName,
                                                            (CFE_ES_ChildTaskMainFuncPtr_t)&SBN_RecvPeerTask,
                                                            NULL,
                                                            CFE_PLATFORM_ES_DEFAULT_STACK_SIZE
                                                                + 2 * sizeof(SBN_RecvPeerTaskData_t),
                                                            0,
                                                            0);
                        /* TODO: more accurate stack size required */

                        if (CFE_Status != CFE_SUCCESS)
                        {
                            EVSSendErr(SBN_PEER_EID, "error creating task for %d", (int)(Peer->ProcessorID));
                            return SBN_ERROR;
                        } /* end if */
                    } /* end if */
                }
                else
                {
                    Net->IfOps->PollPeer(Peer);
                } /* end if */
            } /* end for */
        } /* end if */
    } /* end for */

    return SBN_SUCCESS;
} /* end SBN_PeerPoll */
#endif
