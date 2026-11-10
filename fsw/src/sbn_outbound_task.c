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
//jphfix #include "cfe_es.h" /* PerfLog */
//jphfix #include "cfe_platform_cfg.h"

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

    SBN_Status = Net->IfOps->Send(Peer, MsgType, MsgSz, Msg);

    if (SBN_Status != SBN_SUCCESS)
    {
        Peer->Counts.SendErrCnt++;
    }
    else
    {
        Peer->Counts.SendCnt++;
    } /* end if */

    /* for clients that need a poll or heartbeat, update time even when failing */
    OS_GetLocalTime(&Peer->LastSend);

    return SBN_Status;
} /* end SBN_SendNetMsg */


/**
 * \brief When a peer is connected, a task is created to listen to the relevant
 * pipe for messages to send to that peer.
 */
void SBN_SendJob(SBN_SendTaskData_t *D)
{
    SBN_Filter_Ctx_t   Filter_Context;
    CFE_MSG_Size_t     MsgSz;
    SBN_MsgSz_t        SBN_MsgSz;
    SBN_ModuleIdx_t    FilterIdx;
    int32 TimeOut;

    MsgSz                         = 0;
    Filter_Context.MyProcessorID  = CFE_PSP_GetProcessorId();
    Filter_Context.MySpacecraftID = CFE_PSP_GetSpacecraftId();

    if (!D->Peer->Connected)
    {
        return;
    }

    /* JPHFIX: In polling mode this should be 0 (CHECK) */
    TimeOut = 100;

    if (CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&D->MsgPtr, D->Peer->Pipe, TimeOut) != CFE_SUCCESS)
    {
        return;
    } /* end if */

    Filter_Context.PeerProcessorID  = D->Peer->ProcessorID;
    Filter_Context.PeerSpacecraftID = D->Peer->SpacecraftID;

    for (FilterIdx = 0; FilterIdx < D->Peer->FilterCnt; FilterIdx++)
    {
        if (D->Peer->Filters[FilterIdx]->FilterSend == NULL)
        {
            continue;
        } /* end if */

        D->Status = (D->Peer->Filters[FilterIdx]->FilterSend)(D->MsgPtr, &Filter_Context);
        if (D->Status == SBN_IF_EMPTY) /* filter requests not sending this msg, see below for loop */
        {
            break;
        } /* end if */

        if (D->Status != SBN_SUCCESS)
        {
            break;
        } /* end if */
    } /* end for */

    if (FilterIdx < D->Peer->FilterCnt)
    {
        /* one of the above filters suggested rejecting this message */
        /* JPHFIX: this will hold the buffer. needs to loop */
        return;
    } /* end if */

    if (CFE_MSG_GetSize(D->MsgPtr, &MsgSz) != CFE_SUCCESS)
    {
        /* JPHFIX: this will hold the buffer. needs to loop */
        return;
    } /* end if */

    SBN_MsgSz = (SBN_MsgSz_t)MsgSz;

    D->Status = SBN_SendNetMsg(SBN_APP_MSG, SBN_MsgSz, D->MsgPtr, D->Peer);

} /* end SBN_SendTask() */



#ifdef jphfix
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
        Peer->Counts.SendErrCnt++;
    }
    else
    {
        Peer->Counts.SendCnt++;
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
#endif

#ifdef jphfix
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
#endif

#ifdef jphfix
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
#endif
