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
#include "sbn_worker_task.h"
#include "cfe_mission_cfg.h"
#include "osapi.h"

#define SBN_WORKER_QUEUE_MASK (SBN_WORKER_QUEUE_DEPTH - 1)

static SBN_WorkerTask_WorkQueue_t SBN_WorkQueue;

static inline bool SBN_WorkerTask_IsPending(void)
{
    return (SBN_WorkQueue.NumPosted != SBN_WorkQueue.NumClaimed);
}

static inline SBN_WorkerTask_WorkItem_t *SBN_WorkerTask_GetJob(uint32 Count)
{
    uint32 Idx = (Count & SBN_WORKER_QUEUE_MASK);
    return &SBN_WorkQueue.WorkItems[Idx];
}

/* JPHFIX: This may not need to be public */
bool SBN_WorkerTask_IsIdle(void)
{
    return (SBN_WorkQueue.NumPosted == SBN_WorkQueue.NumCompleted);
}

void SBN_WorkerTask_GetAssignment(SBN_WorkerTask_WorkItem_t *Assignment)
{
    OS_time_t WakeupTime;

    OS_GetLocalTime(&WakeupTime);

    OS_CondVarLock(SBN_WorkQueue.Sync);

    ++SBN_WorkQueue.NumCompleted;
    Assignment->JobType = SBN_WorkerTask_JobType_NONE;

    if (!SBN_WorkQueue.ShouldRun)
    {
        /* because the main thread might be waiting for idle-ness */
        OS_CondVarBroadcast(SBN_WorkQueue.Sync);
    }

    while(!SBN_WorkQueue.ShouldExit)
    {
        if (SBN_WorkerTask_IsPending())
        {
            /* snapshot the job description.  This is small. */
            *Assignment = *(SBN_WorkerTask_GetJob(SBN_WorkQueue.NumClaimed));
            ++SBN_WorkQueue.NumClaimed;

            /* NOTE: even if the job type is NONE, we claimed it and thus incremented the count.
             * we must exit this loop, do the job, then come back so it will be properly "Completed" */
            break;
        }

        /* this is done with a timeout so we periodically check ShouldExit */
        WakeupTime = OS_TimeAdd(WakeupTime, OS_TimeFromTotalMilliseconds(SBN_WORKER_WAIT_TIME_MSEC));
        OS_CondVarTimedWait(SBN_WorkQueue.Sync, &WakeupTime);
    }
    OS_CondVarUnlock(SBN_WorkQueue.Sync);
}

void SBN_WorkerTask_EntryPoint(void)
{
    SBN_WorkerTask_Context_t *Ctxt;

    OS_CondVarLock(SBN_WorkQueue.Sync);
    Ctxt = &SBN_WorkQueue.Tasks[SBN_WorkQueue.NumWorkers];
    ++SBN_WorkQueue.NumWorkers;

    /* When this worker gets its first assignment, it will increment the completed count.
     * This first time through it had no real job yet.  So pretend this startup was a job. */
    ++SBN_WorkQueue.NumPosted;
    SBN_WorkQueue.NumClaimed = SBN_WorkQueue.NumPosted;

    OS_CondVarBroadcast(SBN_WorkQueue.Sync);
    OS_CondVarUnlock(SBN_WorkQueue.Sync);

    while (true)
    {
        /* this will pend/block until something interesting is there */
        SBN_WorkerTask_GetAssignment(&Ctxt->Assignment);

        if (SBN_WorkQueue.ShouldExit)
        {
            break;
        }

        if (SBN_WorkQueue.ShouldRun && Ctxt->Assignment.JobType != SBN_WorkerTask_JobType_NONE)
        {
            /* JPHFIX: Do the assignment */
            ++Ctxt->NumCompleted;
        }
    }

    OS_CondVarLock(SBN_WorkQueue.Sync);
    --SBN_WorkQueue.NumWorkers;
    OS_CondVarBroadcast(SBN_WorkQueue.Sync);
    OS_CondVarUnlock(SBN_WorkQueue.Sync);
}

SBN_Status_t SBN_WorkerTask_Setup(void)
{
    uint32 i;
    char TaskName[CFE_MISSION_MAX_API_LEN];
    CFE_Status_t Status;

    memset(&SBN_WorkQueue, 0, sizeof(SBN_WorkQueue));

    /* We can use a condition variable to sync the threads */
    OS_CondVarCreate(&SBN_WorkQueue.Sync, "SBN_Worker", 0);

    for (i=0; i < SBN_NUM_WORKER_TASKS; ++i)
    {
        snprintf(TaskName, sizeof(TaskName), "SBN_WORK_%02u", (unsigned int)i);
        Status = CFE_ES_CreateChildTask(&SBN_WorkQueue.Tasks[i].TaskId, TaskName, SBN_WorkerTask_EntryPoint, NULL, SBN_WORKER_TASK_STACK_SIZE, SBN_WORKER_TASK_PRIORITY, 0);
        if (Status != CFE_SUCCESS)
        {
            break;
        }

        /* Wait for the child to grab its slot */
        OS_CondVarLock(SBN_WorkQueue.Sync);
        while (SBN_WorkQueue.NumWorkers != i)
        {
            OS_CondVarWait(SBN_WorkQueue.Sync);
        }
        OS_CondVarUnlock(SBN_WorkQueue.Sync);
    }

    return SBN_SUCCESS;
}

void SBN_WorkerTask_Shutdown(void)
{
    OS_CondVarLock(SBN_WorkQueue.Sync);
    SBN_WorkQueue.ShouldExit = true;
    while(SBN_WorkQueue.NumWorkers != 0)
    {
        OS_CondVarSignal(SBN_WorkQueue.Sync);
        OS_CondVarWait(SBN_WorkQueue.Sync);
    }
    OS_CondVarUnlock(SBN_WorkQueue.Sync);
}

void SBN_WorkerTask_Pause(void)
{
    OS_CondVarLock(SBN_WorkQueue.Sync);

    /* Instructs all workers to pause.  When they get to their stopping point,
     * they will also broadcast the signal so we can check it again */
    SBN_WorkQueue.ShouldRun = false;

    while (!SBN_WorkerTask_IsIdle())
    {
        OS_CondVarWait(SBN_WorkQueue.Sync);
    }

    OS_CondVarUnlock(SBN_WorkQueue.Sync);
}

void SBN_WorkerTask_Resume(void)
{
    OS_CondVarLock(SBN_WorkQueue.Sync);

    /* Instructs all workers to resume.
     * This broadcasts the signal so workers will check */
    SBN_WorkQueue.ShouldRun = true;


    /* JPHFIX: This broadcast might not be needed.  For the whole time it was paused,
     * new job scheduling was also blocked too.  This means the queue is guaranteed empty. */
    OS_CondVarBroadcast(SBN_WorkQueue.Sync);
    OS_CondVarUnlock(SBN_WorkQueue.Sync);
}

bool SBN_WorkerTask_ScheduleJob(const SBN_WorkerTask_WorkItem_t *Job)
{
    uint32 Depth;
    bool GotSpace;

    OS_CondVarLock(SBN_WorkQueue.Sync);

    Depth = SBN_WorkQueue.NumPosted - SBN_WorkQueue.NumCompleted;
    if (SBN_WorkQueue.ShouldRun)
    {
        GotSpace = (Depth < SBN_WORKER_QUEUE_DEPTH);
    }
    else
    {
        GotSpace = false;
    }

    if (GotSpace)
    {
        /* put job into the queue */
        *(SBN_WorkerTask_GetJob(SBN_WorkQueue.NumPosted)) = *Job;
        ++SBN_WorkQueue.NumPosted;

        /* we only need to signal a single task to come get this. */
        OS_CondVarSignal(SBN_WorkQueue.Sync);
    }

    OS_CondVarUnlock(SBN_WorkQueue.Sync);

    return GotSpace;
}

#ifdef jphfix
//jphfix #include "cfe_es.h" /* PerfLog */
//jphfix #include "cfe_platform_cfg.h"

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
SBN_Status_t SBN_SendNetMsg(SBN_MsgType_t MsgType, SBN_MsgSz_t MsgSz, void *Msg, SBN_PeerInterface_t *Peer)
{
    SBN_NetInterface_t *Net;
    SBN_Status_t        SBN_Status;

    Net = Peer->Net;

    if (CFE_RESOURCEID_TEST_DEFINED(Peer->SendTaskID))
    {
        if (OS_MutSemTake(SBN_AppData.SendMutex) != OS_SUCCESS)
        {
            EVSSendErr(SBN_PEER_EID, "unable to take send mutex");
            return SBN_ERROR;
        } /* end if */
    } /* end if */

    SBN_Status = Net->IfOps->Send(Peer, MsgType, MsgSz, Msg);

    if (SBN_Status != SBN_SUCCESS)
    {
        Peer->SendErrCnt++;
    }
    else
    {
        Peer->SendCnt++;
    } /* end if */

    /* for clients that need a poll or heartbeat, update time even when failing */
    OS_GetLocalTime(&Peer->LastSend);

    if (CFE_RESOURCEID_TEST_DEFINED(Peer->SendTaskID))
    {
        if (OS_MutSemGive(SBN_AppData.SendMutex) != OS_SUCCESS)
        {
            EVSSendErr(SBN_PEER_EID, "unable to give send mutex");
            return SBN_ERROR;
        } /* end if */
    } /* end if */

    return SBN_Status;
} /* end SBN_SendNetMsg */

/**
 * \brief When a peer is connected, a task is created to listen to the relevant
 * pipe for messages to send to that peer.
 */
void SBN_SendTask(void)
{
    SBN_SendTaskData_t D;
    SBN_Filter_Ctx_t   Filter_Context;
    CFE_MSG_Size_t     MsgSz;
    SBN_MsgSz_t        SBN_MsgSz;
    SBN_ModuleIdx_t    FilterIdx;

    MsgSz                         = 0;
    Filter_Context.MyProcessorID  = CFE_PSP_GetProcessorId();
    Filter_Context.MySpacecraftID = CFE_PSP_GetSpacecraftId();

    memset(&D, 0, sizeof(D));

    CFE_ES_GetTaskID(&D.SendTaskID);

    for (D.NetIdx = 0; D.NetIdx < SBN_AppData.NetCnt; D.NetIdx++)
    {
        D.Net = &SBN_AppData.Nets[D.NetIdx];
        for (D.PeerIdx = 0; D.PeerIdx < D.Net->PeerCnt; D.PeerIdx++)
        {
            D.Peer = &D.Net->Peers[D.PeerIdx];
            if (CFE_RESOURCEID_TEST_EQUAL(D.Peer->SendTaskID, D.SendTaskID))
            {
                break;
            } /* end if */
        } /* end for */

        if (D.PeerIdx < D.Net->PeerCnt)
        {
            break; /* found a ringer */
        } /* end if */
    } /* end for */

    if (D.NetIdx == SBN_AppData.NetCnt)
    {
        EVSSendErr(SBN_PEER_EID, "error connecting send task");
        return;
    } /* end if */

    while (1)
    {
        if (!D.Peer->Connected)
        {
            OS_TaskDelay(SBN_PLATFORM_MAIN_LOOP_DELAY);
            continue;
        } /* end if */

        if (CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&D.MsgPtr, D.Peer->Pipe, CFE_SB_PEND_FOREVER) != CFE_SUCCESS)
        {
            break;
        } /* end if */

        Filter_Context.PeerProcessorID  = D.Peer->ProcessorID;
        Filter_Context.PeerSpacecraftID = D.Peer->SpacecraftID;

        for (FilterIdx = 0; FilterIdx < D.Peer->FilterCnt; FilterIdx++)
        {
            if (D.Peer->Filters[FilterIdx]->FilterSend == NULL)
            {
                continue;
            } /* end if */

            D.Status = (D.Peer->Filters[FilterIdx]->FilterSend)(D.MsgPtr, &Filter_Context);
            if (D.Status == SBN_IF_EMPTY) /* filter requests not sending this msg, see below for loop */
            {
                break;
            } /* end if */

            if (D.Status != SBN_SUCCESS)
            {
                break;
            } /* end if */
        } /* end for */

        if (FilterIdx < D.Peer->FilterCnt)
        {
            /* one of the above filters suggested rejecting this message */
            continue;
        } /* end if */

        if (CFE_MSG_GetSize(D.MsgPtr, &MsgSz) != CFE_SUCCESS)
        {
            continue;
        } /* end if */

        SBN_MsgSz = (SBN_MsgSz_t)MsgSz;

        D.Status = SBN_SendNetMsg(SBN_APP_MSG, SBN_MsgSz, D.MsgPtr, D.Peer);

        if (D.Status == SBN_ERROR)
        {
            break;
        } /* end if */
    } /* end while */

    /* mark peer as not having a task so that sending will create a new one */
    D.Peer->SendTaskID = CFE_ES_TASKID_UNDEFINED;
} /* end SBN_SendTask() */

/**
 * Iterate through all peers, examining the pipe to see if there are messages
 * I need to send to that peer.
 */
static SBN_Status_t SBN_CheckPeerPipes(void)
{
    SBN_Status_t         SBN_Status;
    CFE_Status_t         CFE_Status;
    int                  ReceivedFlag;
    int                  iter;
    CFE_MSG_Message_t   *MsgPtr;
    CFE_MSG_Size_t       MsgSz;
    SBN_MsgSz_t          SBN_MsgSz;
    SBN_Filter_Ctx_t     Filter_Context;
    SBN_NetIdx_t         NetIdx;
    SBN_NetInterface_t  *Net;
    SBN_PeerIdx_t        PeerIdx;
    SBN_ModuleIdx_t      FilterIdx;
    SBN_PeerInterface_t *Peer;
    char                 SendTaskName[32];

    MsgPtr                        = NULL;
    MsgSz                         = 0;
    Filter_Context.MyProcessorID  = CFE_PSP_GetProcessorId();
    Filter_Context.MySpacecraftID = CFE_PSP_GetSpacecraftId();

    /**
     * \note This processes one message per peer, then start again until no
     * peers have pending messages. At max only process SBN_PLATFORM_MAX_MSG_PER_WAKEUP
     * per peer per wakeup otherwise I will starve other processing.
     */
    for (iter = 0; iter < SBN_PLATFORM_MAX_MSG_PER_WAKEUP; iter++)
    {
        ReceivedFlag = 0;

        for (NetIdx = 0; NetIdx < SBN_AppData.NetCnt; NetIdx++)
        {
            Net = &SBN_AppData.Nets[NetIdx];

            for (PeerIdx = 0; PeerIdx < Net->PeerCnt; PeerIdx++)
            {
                Peer = &Net->Peers[PeerIdx];

                // Poll peer here to detect disconnections and to reconnect
                if (Net->IfOps->PollPeer(Peer) != SBN_SUCCESS)
                {
                    EVSSendErr(SBN_PEERTASK_EID, "failed to poll peer %d:%d", Peer->SpacecraftID, Peer->ProcessorID);
                }

                if (Peer->Connected == 0)
                {
                    EVSSendDbg(SBN_PEERTASK_EID, "not connected to peer %d:%d", Peer->SpacecraftID, Peer->ProcessorID);
                    continue;
                } /* end if */

                EVSSendDbg(SBN_PEERTASK_EID, "SBN connected to peer %d:%d", Peer->SpacecraftID, Peer->ProcessorID);

                if (Peer->TaskFlags & SBN_TASK_SEND)
                {
                    if (!CFE_RESOURCEID_TEST_DEFINED(Peer->SendTaskID))
                    {
                        /* TODO: logic/controls to prevent hammering? */
                        snprintf(SendTaskName,
                                 32,
                                 "sendT_%d_%d_%d",
                                 (int)NetIdx,
                                 (int)(Peer->ProcessorID),
                                 (int)(Peer->SpacecraftID));

                        CFE_Status =
                            CFE_ES_CreateChildTask(&(Peer->SendTaskID),
                                                   SendTaskName,
                                                   (CFE_ES_ChildTaskMainFuncPtr_t)&SBN_SendTask,
                                                   NULL,
                                                   CFE_PLATFORM_ES_DEFAULT_STACK_SIZE + 2 * sizeof(SBN_SendTaskData_t),
                                                   0,
                                                   0);

                        if (CFE_Status != CFE_SUCCESS)
                        {
                            EVSSendErr(SBN_PEER_EID,
                                       "error creating send task for peer %d:%d",
                                       Peer->SpacecraftID,
                                       Peer->ProcessorID);

                            return SBN_ERROR;
                        } /* end if */
                    } /* end if */

                    continue;
                } /* end if */

                /* if peer data is not in use, go to next peer */
                if (CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&MsgPtr, Peer->Pipe, CFE_SB_POLL) != CFE_SUCCESS)
                {
                    continue;
                } /* end if */

                ReceivedFlag = 1;

                Filter_Context.PeerProcessorID  = Peer->ProcessorID;
                Filter_Context.PeerSpacecraftID = Peer->SpacecraftID;

                for (FilterIdx = 0; FilterIdx < Peer->FilterCnt; FilterIdx++)
                {
                    if (Peer->Filters[FilterIdx]->FilterSend == NULL)
                    {
                        continue;
                    } /* end if */

                    SBN_Status = (Peer->Filters[FilterIdx]->FilterSend)(MsgPtr, &Filter_Context);

                    if (SBN_Status == SBN_IF_EMPTY) /* filter requests not sending this msg, see below for loop */
                    {
                        break;
                    } /* end if */

                    if (SBN_Status != SBN_SUCCESS)
                    {
                        /* something fatal happened, exit */
                        return SBN_Status;
                    } /* end if */
                } /* end for */

                if (FilterIdx < Peer->FilterCnt)
                {
                    /* one of the above filters suggested rejecting this message */
                    continue;
                } /* end if */

                if (CFE_MSG_GetSize(MsgPtr, &MsgSz) != CFE_SUCCESS)
                {
                    continue;
                } /* end if */

                SBN_MsgSz = (SBN_MsgSz_t)MsgSz;

                SBN_SendNetMsg(SBN_APP_MSG, SBN_MsgSz, MsgPtr, Peer);
            } /* end for */
        } /* end for */

        if (!ReceivedFlag)
        {
            break;
        } /* end if */
    } /* end for */
    return SBN_SUCCESS;
} /* end SBN_CheckPeerPipes */

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