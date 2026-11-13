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

#include "sbn_app.h"
#include <string.h>
#include <arpa/inet.h>
#include "cfe_msgids.h"
#include "sbn_pack.h"
#include "sbn_error.h"
#include "sbn_outbound_task.h"

// TODO: instead of using void * for the buffer for SBN messages, use
// a struct that has the SBN header in packed bytes.

SBN_SubChange_t *SBN_Subs_GetChangeRec(SBN_SubChangeQ_t *ChangeQ, uint32 Version)
{
    SBN_SubChange_t *ChangeRec;

    ChangeRec = &ChangeQ->Change[Version & SBN_SUBSCRIPTION_UPDATE_QUEUE_MASK];

    return ChangeRec;
}

SBN_SubChange_t *SBN_Subs_GetPrevChangeRec(SBN_SubChangeQ_t *ChangeQ, uint32 Version)
{
    uint32 Age;
    SBN_SubChange_t *ChangeRec;

    Age = ChangeQ->CurrVersion - Version;
    if (Age > (SBN_SUBSCRIPTION_UPDATE_QUEUE_DEPTH / 2))
    {
        /* too old! */
        ChangeRec = NULL;
    }
    else
    {
        ChangeRec = SBN_Subs_GetChangeRec(ChangeQ, Version);
    }

    return ChangeRec;
}

void SBN_Subs_InvalidateChangeQ(SBN_SubChangeQ_t *ChangeQ)
{
    /* Step by an amount larger than the depth, will cause all watchers to re-sync completely */
    ChangeQ->CurrVersion += SBN_SUBSCRIPTION_UPDATE_QUEUE_DEPTH;
}

void SBN_Subs_RegisterChange(SBN_SubChangeQ_t *ChangeQ, bool IsSubscribe, CFE_SB_MsgId_t MsgId, CFE_SB_Qos_t Qos)
{
    SBN_SubChange_t *ChangeRec;

    ChangeRec = SBN_Subs_GetChangeRec(ChangeQ, ChangeQ->CurrVersion);

    ChangeRec->IsSubscribe = IsSubscribe;
    ChangeRec->MsgId = MsgId;
    ChangeRec->Qos = Qos;

    ++ChangeQ->CurrVersion;
}

/**
 * Informs the software bus to send this application all subscription requests.
 */
SBN_Status_t SBN_SendSubsRequests(void)
{
    CFE_Status_t            CFE_Status;
    CFE_MSG_CommandHeader_t CmdMsg;
    static CFE_SB_MsgId_t   SB_SUB_RPT_CTRL_MID = CFE_SB_MSGID_RESERVED;

    /* cache the local MID Values here, this avoids repeat lookups */
    if (!CFE_SB_IsValidMsgId(SB_SUB_RPT_CTRL_MID))
    {
        SB_SUB_RPT_CTRL_MID = CFE_SB_ValueToMsgId(CFE_SB_SUB_RPT_CTRL_MID);
    }

    /* Turn on SB subscription reporting */
    CFE_MSG_Init((CFE_MSG_Message_t *)&CmdMsg, SB_SUB_RPT_CTRL_MID, sizeof(CmdMsg));
    CFE_MSG_SetFcnCode((CFE_MSG_Message_t *)&CmdMsg, CFE_SB_ENABLE_SUB_REPORTING_CC);
    CFE_Status = CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&CmdMsg, true);
    if (CFE_Status != CFE_SUCCESS)
    {
        EVSSendErr(SBN_SUB_EID, "Unable to turn on sub reporting (status=%d)", CFE_Status);
        return SBN_ERROR;
    } /* end if */

    /* Request a list of previous subscriptions from SB */
    CFE_MSG_SetFcnCode((CFE_MSG_Message_t *)&CmdMsg, CFE_SB_SEND_PREV_SUBS_CC);
    CFE_Status = CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&CmdMsg, true);
    if (CFE_Status != CFE_SUCCESS)
    {
        EVSSendErr(SBN_SUB_EID, "Unable to send prev subs request (status=%d)", CFE_Status);
        return SBN_ERROR;
    } /* end if */

    return SBN_SUCCESS;
} /* end SBN_SendSubsRequests */

/**
 * \brief Sends a local subscription over the wire to a peer.
 *
 * @param[in] SubType Whether this is a subscription or unsubscription.
 * @param[in] MsgID The CCSDS message ID being (un)subscribed.
 * @param[in] QoS The CCSDS quality of service being (un)subscribed.
 * @param[in] Peer The Peer interface
 */
static SBN_Status_t
SBN_SendLocalSubToPeer(SBN_MsgType_t SubType, CFE_SB_MsgId_t MsgID, CFE_SB_Qos_t QoS, SBN_PeerInterface_t *Peer)
{
    uint8           Buf[SBN_PACKED_SUB_SZ];
    SBN_PackState_t Pack;
    SBN_Pack_Init(&Pack, &Buf, SBN_PACKED_SUB_SZ, 0);
    SBN_Pack_Data(&Pack, (void *)SBN_IDENT, SBN_IDENT_LEN);
    SBN_Pack_UInt16(&Pack, 1);

    SBN_Pack_MsgID(&Pack, MsgID);
    SBN_Pack_Data(&Pack, &QoS, sizeof(QoS)); /* 2 uint8's */

    EVSSendDbg(SBN_PEER_EID, "send local sub to peer %d:%d", Peer->SpacecraftID, Peer->ProcessorID);
    return SBN_SendNetMsg(SubType, SBN_Pack_GetPosition(&Pack), Buf, Peer);
} /* end SendLocalSubToPeer */

/**
 * \brief Sends all local subscriptions over the wire to a peer.
 *
 * @param[in] Peer The peer interface.
 */
SBN_Status_t SBN_SendLocalSubsToPeer(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    SBN_Status_t Status;
    SBN_PackState_t Pack;
    uint32 SubsVersion;

    SubsVersion = SBN_AppData.SubChangeQ.CurrVersion;

    SBN_Pack_Init(&Pack, D->DataPtr, D->MaxDataSz, 0);

    SBN_Pack_Data(&Pack, (void *)SBN_IDENT, SBN_IDENT_LEN);
    SBN_Pack_UInt16(&Pack, SBN_AppData.SubCnt);

    int i;
    for (i = 0; i < SBN_AppData.SubCnt; i++)
    {
        SBN_Pack_MsgID(&Pack, SBN_AppData.Subs[i].MsgID);
        /* 2 uint8's */
        SBN_Pack_Data(&Pack, &SBN_AppData.Subs[i].QoS, sizeof(SBN_AppData.Subs[i].QoS));
    } /* end for */

    EVSSendDbg(SBN_PEER_EID, "send local subs to peer %d:%d",
        (int)PeerRec->Intf.SpacecraftID,
        (int)PeerRec->Intf.ProcessorID);

    D->ActualDataSz = SBN_Pack_GetPosition(&Pack);
    D->MsgType = SBN_SUB_MSG;

    Status = SBN_Outbound_SendToPeer(&PeerRec->Intf, D);

    if (Status == SBN_SUCCESS)
    {
        /* Record that we sent this version of the subs table to the peer */
        PeerRec->PeerState.PeerSentSubVersion = SubsVersion;
    }

    return Status;
} /* end SBN_SendLocalSubsToPeer */

/**
 * Utility to find the subscription index (SBN_AppData.Subs)
 * that is subscribed to the CCSDS message ID.
 *
 * @param[out] IdxPtr The subscription index found.
 * @param[in] MsgID The CCSDS message ID of the subscription being sought.
 * @return true if found.
 */
static bool SBN_CheckSubList(SBN_SubCnt_t *IdxPtr, SBN_Subs_t *SubList, SBN_SubCnt_t SubCnt, CFE_SB_MsgId_t MsgID)
{
    SBN_SubCnt_t i;

    for (i = 0; i < SubCnt; i++)
    {
        if (CFE_SB_MsgId_Equal(SubList[i].MsgID, MsgID))
        {
            if (IdxPtr)
            {
                *IdxPtr = i;
            } /* end if */

            return true;
        } /* end if */
    } /* end for */

    return false;
} /* end IsMsgIDSub */

/**
 * Utility to find the subscription index (SBN_AppData.Subs)
 * that is subscribed to the CCSDS message ID.
 *
 * @param[out] IdxPtr The subscription index found.
 * @param[in] MsgID The CCSDS message ID of the subscription being sought.
 * @return true if found.
 */
static bool SBN_IsLocalMsgIDSub(SBN_SubCnt_t *IdxPtr, CFE_SB_MsgId_t MsgID)
{
    return SBN_CheckSubList(IdxPtr, SBN_AppData.Subs, SBN_AppData.SubCnt, MsgID);
} /* end IsMsgIDSub */

/**
 * \brief Is this peer subscribed to this message ID? If so, what is the index
 *        of the subscription?
 *
 * @param[out] SubIdxPtr The pointer to the subscription index value.
 * @param[in] MsgID The CCSDS message ID of the subscription being sought.
 * @param[in] Peer The peer interface.
 *
 * @return true if found.
 */
static bool SBN_IsPeerMsgIDSub(SBN_SubCnt_t *SubIdxPtr, CFE_SB_MsgId_t MsgID, SBN_PeerRecord_t *PeerRec)
{
    return SBN_CheckSubList(SubIdxPtr, PeerRec->Intf.Subs, PeerRec->Intf.SubCnt, MsgID);
} /* end IsPeerSubMsgID */

/**
 * \brief I have seen a local subscription, send it on to peers if this is the
 * first instance of a subscription for this message ID.
 *
 * @param[in] MsgID The CCSDS Message ID of the local subscription.
 * @param[in] QoS The CCSDS quality of service of the local subscription.
 */
static SBN_Status_t ProcessLocalSub(CFE_SB_MsgId_t MsgID, CFE_SB_Qos_t QoS)
{
    SBN_SubCnt_t          SubIdx = 0;

    static CFE_SB_MsgId_t LCMD_MID            = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t LHK_TLM_MID         = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t LHKNET_TLM_MID      = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t LHKPEER_TLM_MID     = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t LHKMYSUBS_TLM_MID   = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t LHKPEERSUBS_TLM_MID = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t EVENT_MSG_MID       = CFE_SB_MSGID_RESERVED;

    /* cache the local MID Values here, this avoids repeat lookups */
    if (!CFE_SB_IsValidMsgId(LCMD_MID))
    {
        LCMD_MID            = CFE_SB_ValueToMsgId(SBN_CMD_MID);
        LHK_TLM_MID         = CFE_SB_ValueToMsgId(SBN_HK_TLM_MID);
        LHKNET_TLM_MID      = CFE_SB_ValueToMsgId(SBN_HKNET_TLM_MID);
        LHKPEER_TLM_MID     = CFE_SB_ValueToMsgId(SBN_HKPEER_TLM_MID);
        LHKMYSUBS_TLM_MID   = CFE_SB_ValueToMsgId(SBN_HKMYSUBS_TLM_MID);
        LHKPEERSUBS_TLM_MID = CFE_SB_ValueToMsgId(SBN_HKPEERSUBS_TLM_MID);
        EVENT_MSG_MID       = CFE_SB_ValueToMsgId(CFE_EVS_LONG_EVENT_MSG_MID);
    }

    /* don't send event messages */
    if (CFE_SB_MsgId_Equal(MsgID, EVENT_MSG_MID))
        return SBN_SUCCESS;

    /* don't send SBN messages */
    if (CFE_SB_MsgId_Equal(MsgID, LCMD_MID) || CFE_SB_MsgId_Equal(MsgID, LHK_TLM_MID)
        || CFE_SB_MsgId_Equal(MsgID, LHKNET_TLM_MID) || CFE_SB_MsgId_Equal(MsgID, LHKPEER_TLM_MID)
        || CFE_SB_MsgId_Equal(MsgID, LHKMYSUBS_TLM_MID) || CFE_SB_MsgId_Equal(MsgID, LHKPEERSUBS_TLM_MID))
        return SBN_SUCCESS;

    /* if there is already an entry for this msg id,just incr InUseCtr */
    if (SBN_IsLocalMsgIDSub(&SubIdx, MsgID))
    {
        SBN_AppData.Subs[SubIdx].InUseCtr++;
        EVSSendDbg(SBN_SUB_EID, "local sub already exists: in use: %d", SBN_AppData.Subs[SubIdx].InUseCtr);
        /* does not send to peers, as they already know */
        return SBN_SUCCESS;
    } /* end if */

    if (SBN_AppData.SubCnt >= SBN_MISSION_MAX_SUBS_PER_PEER)
    {
        EVSSendErr(SBN_SUB_EID,
                   "local subscription ignored for MsgID 0x%04X, max (%d) met",
                   CFE_SB_MsgIdToValue(MsgID),
                   SBN_MISSION_MAX_SUBS_PER_PEER);
        return SBN_ERROR;
    } /* end if */

    /* log new entry into Subs array */
    SBN_AppData.Subs[SBN_AppData.SubCnt].InUseCtr = 1;
    SBN_AppData.Subs[SBN_AppData.SubCnt].MsgID    = MsgID;
    SBN_AppData.Subs[SBN_AppData.SubCnt].QoS      = QoS;
    SBN_AppData.SubCnt++;

    /* track this change individually */
    SBN_Subs_RegisterChange(&SBN_AppData.SubChangeQ, true, MsgID, QoS);

    return SBN_SUCCESS;
} /* end ProcessLocalSub */

/**
 * \brief I have seen a local unsubscription, send it on to peers if this is the
 * last instance of a subscription for this message ID.
 *
 * @param[in] MsgID The CCSDS Message ID of the local unsubscription.
 */
static SBN_Status_t ProcessLocalUnsub(CFE_SB_MsgId_t MsgID)
{
    SBN_SubCnt_t          SubIdx;

    /* find idx of matching subscription */
    if (!SBN_IsLocalMsgIDSub(&SubIdx, MsgID))
    {
        return SBN_SUCCESS; /* or should this be error? */
    } /* end if */

    SBN_AppData.Subs[SubIdx].InUseCtr--;

    /* do not modify the array and tell peers
    ** until the # of local subscriptions = 0
    */
    if (SBN_AppData.Subs[SubIdx].InUseCtr > 0)
    {
        return SBN_SUCCESS;
    } /* end if */

    /* remove sub from array for and
    ** shift all subscriptions in higher elements to fill the gap
    ** note that the Subs[] array has one extra element to allow for an
    ** unsub from a full table.
    */
    for (; SubIdx < SBN_AppData.SubCnt; SubIdx++)
    {
        memcpy(&SBN_AppData.Subs[SubIdx], &SBN_AppData.Subs[SubIdx + 1], sizeof(SBN_Subs_t));
    } /* end for */

    SBN_AppData.SubCnt--;

    /* track this change individually */
    SBN_Subs_RegisterChange(&SBN_AppData.SubChangeQ, false, MsgID, CFE_SB_DEFAULT_QOS);

    return SBN_SUCCESS;
} /* end ProcessLocalUnsub */

/**
 * \brief Check the local pipe for subscription messages. Send them on to peers
 *        if there are any (new)subscriptions.
 *
 * @return SBN_SUCCESS if subscriptions received, SBN_IF_EMPTY if no subs recvd, SBN_ERROR otherwise.
 */
SBN_Status_t SBN_CheckSubscriptionPipe(void)
{
    CFE_Status_t CFE_Status;

    CFE_SB_AllSubscriptionsTlm_t   *MsgPtr = NULL; /* largest message format */
    CFE_SB_SingleSubscriptionTlm_t *SingleMsgPtr;  /* utility "cast" */
    CFE_SB_MsgId_t                  MsgId              = CFE_SB_INVALID_MSG_ID;
    static CFE_SB_MsgId_t           SB_ONESUB_TLM_MID  = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t           SB_ALLSUBS_TLM_MID = CFE_SB_MSGID_RESERVED;

    /* cache the local MID Values here, this avoids repeat lookups */
    if (!CFE_SB_IsValidMsgId(SB_ONESUB_TLM_MID))
    {
        SB_ONESUB_TLM_MID  = CFE_SB_ValueToMsgId(CFE_SB_ONESUB_TLM_MID);
        SB_ALLSUBS_TLM_MID = CFE_SB_ValueToMsgId(CFE_SB_ALLSUBS_TLM_MID);
    }

    CFE_Status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&MsgPtr, SBN_AppData.SubPipe, CFE_SB_POLL);
    switch (CFE_Status)
    {
        case CFE_SUCCESS:

            if (CFE_MSG_GetMsgId((CFE_MSG_Message_t *)MsgPtr, &MsgId) != CFE_SUCCESS)
            {
                EVSSendErr(SBN_MSG_EID, "unable to get message id");
                return SBN_ERROR;
            }

            if (CFE_SB_MsgId_Equal(MsgId, SB_ONESUB_TLM_MID))
            {
                SingleMsgPtr = (CFE_SB_SingleSubscriptionTlm_t *)MsgPtr;
                switch (SingleMsgPtr->Payload.SubType)
                {
                    case CFE_SB_SUBSCRIPTION:
                        return ProcessLocalSub(SingleMsgPtr->Payload.MsgId, SingleMsgPtr->Payload.Qos);
                    case CFE_SB_UNSUBSCRIPTION:
                        return ProcessLocalUnsub(SingleMsgPtr->Payload.MsgId);
                    default:
                        EVSSendErr(SBN_SUB_EID,
                                   "unexpected subscription type (%d) in SBN_CheckSubscriptionPipe",
                                   SingleMsgPtr->Payload.SubType);
                        return SBN_ERROR;
                } /* end switch */
            }
            else if (CFE_SB_MsgId_Equal(MsgId, SB_ALLSUBS_TLM_MID))
            {
                return SBN_ProcessAllSubscriptions(MsgPtr);
            }
            else
            {
                EVSSendErr(SBN_MSG_EID,
                           "unexpected message id (0x%04X) on SBN_AppData.SubPipe",
                           CFE_SB_MsgIdToValue(MsgId));
                return SBN_ERROR;
            } /* end switch */

            break;
        case CFE_SB_NO_MESSAGE:
            return SBN_IF_EMPTY;
        default:
            EVSSendErr(SBN_MSG_EID, "err from rcvmsg on sub pipe");
            return SBN_ERROR;
    } /* end switch */

    return SBN_ERROR;
} /* end SBN_CheckSubscriptionPipe */

/**
 * \brief Record keep the subscription locally so that when we no longer have any peers subscribed
 *        to this MID, I unsubscribe from the MID.
 *
 * @param[in] Peer The peer interface.
 * @param[in] MsgID The subscription SBN message ID.
 * @param[in] QoS The subscription quality of service.
 *
 * @return SBN_SUCCESS on successfully adding the sub to my records, otherwise SBN_ERROR
 */
static SBN_Status_t SBN_Subs_AddPeerSub(SBN_PeerRecord_t *PeerRec, CFE_SB_MsgId_t MsgID, CFE_SB_Qos_t QoS)
{
    SBN_SubCnt_t          idx = 0;

    /* if msg id already in the list, ignore */
    if (SBN_IsPeerMsgIDSub(&idx, MsgID, PeerRec))
    {
        return SBN_SUCCESS;
    } /* end if */

    if (PeerRec->Intf.SubCnt >= SBN_MISSION_MAX_SUBS_PER_PEER)
    {
        EVSSendErr(SBN_SUB_EID,
                   "cannot process subscription from ProcessorID %d, max (%d) met",
                   (int)PeerRec->Intf.ProcessorID,
                   SBN_MISSION_MAX_SUBS_PER_PEER);
        return SBN_ERROR;
    } /* end if */

    /* log the subscription in the peer table */
    PeerRec->Intf.Subs[PeerRec->Intf.SubCnt].MsgID = MsgID;
    PeerRec->Intf.Subs[PeerRec->Intf.SubCnt].QoS   = QoS;
    PeerRec->Intf.SubCnt++;

    OS_printf("%s(): Add Sub=%04x total=%u\n",__func__, CFE_SB_MsgIdToValue(MsgID), (unsigned int)PeerRec->Intf.SubCnt);


    /* Track this individual change */
    SBN_Subs_RegisterChange(&PeerRec->PeerState.Q, true, MsgID, QoS);

    return SBN_SUCCESS;
} /* end AddSub */

/**
 * \brief Process a subscription from a peer.
 *
 * @param[in] Peer The peer interface.
 * @param[in] MsgID The subscription SBN message ID.
 * @param[in] QoS The subscription quality of service.
 *
 * @return SBN_SUCCESS on successfully handling subscription from peer, otherwise SBN_ERROR
 */
SBN_Status_t SBN_ProcessSubFromPeer(SBN_PeerRecord_t *PeerRec, CFE_SB_MsgId_t MsgID, CFE_SB_Qos_t QoS)
{
    SBN_ModuleIdx_t  FilterIdx;
    SBN_Filter_Ctx_t Filter_Context;
    SBN_Status_t     SBN_Status;

    Filter_Context.MyProcessorID    = CFE_PSP_GetProcessorId();
    Filter_Context.MySpacecraftID   = CFE_PSP_GetSpacecraftId();
    Filter_Context.PeerProcessorID  = PeerRec->Intf.ProcessorID;
    Filter_Context.PeerSpacecraftID = PeerRec->Intf.SpacecraftID;

    for (FilterIdx = 0; FilterIdx < PeerRec->Intf.FilterCnt; FilterIdx++)
    {
        if (PeerRec->Intf.Filters[FilterIdx]->RemapMID == NULL)
        {
            continue;
        } /* end if */

        SBN_Status = (PeerRec->Intf.Filters[FilterIdx]->RemapMID)(&MsgID, &Filter_Context);

        if (SBN_Status != SBN_SUCCESS)
        {
            return SBN_Status;
        } /* end if */
    } /* end for */

    return SBN_Subs_AddPeerSub(PeerRec, MsgID, QoS);
} /* ProcessSubFromPeer */

/**
 * \brief Process a subscription message from a peer.
 *
 * @param[in] Peer The peer interface
 * @param[in] Msg The subscription SBN message.
 *
 * @return SBN_SUCCESS on successfully handling all subscriptions from peer, otherwise SBN_ERROR
 */
SBN_Status_t SBN_ProcessSubsFromPeer(SBN_PeerRecord_t *PeerRec, void *Msg)
{
    SBN_Status_t      SBN_Status;
    SBN_UnpackState_t Pack;
    char              VersionHash[SBN_IDENT_LEN];

    SBN_Unpack_Init(&Pack, Msg, CFE_MISSION_SB_MAX_SB_MSG_SIZE);

    SBN_Unpack_Data(&Pack, VersionHash, SBN_IDENT_LEN);

    if (strncmp(VersionHash, SBN_IDENT, SBN_IDENT_LEN))
    {
        EVSSendErr(SBN_PROTO_EID, "version number mismatch with peer CpuID %d", (int)PeerRec->Intf.ProcessorID);
        return SBN_ERROR;
    }

    uint16 SubCnt = 0;
    SBN_Unpack_UInt16(&Pack, &SubCnt);

    OS_printf("%s(): Got SubCnt=%u!\n",__func__, (unsigned int)SubCnt);

    int SubIdx;
    for (SubIdx = 0; SubIdx < SubCnt; SubIdx++)
    {
        CFE_SB_MsgId_t MsgID = CFE_SB_INVALID_MSG_ID;
        SBN_Unpack_MsgID(&Pack, &MsgID);
        CFE_SB_Qos_t QoS = { 0 };
        SBN_Unpack_Data(&Pack, &QoS, sizeof(QoS));

        SBN_Status = SBN_ProcessSubFromPeer(PeerRec, MsgID, QoS);

        if (SBN_Status != SBN_SUCCESS)
        {
            return SBN_Status;
        } /* end if */
    } /* end for */

    return SBN_SUCCESS;
} /* SBN_ProcessSubsFromPeer */

/**
 * \brief Process an unsubscription message from a peer.
 *
 * @param[in] Peer The peer interface
 * @param[in] MsgID The unsubscription SBN message ID.
 *
 * @return SBN_SUCCESS on successful unsubscription from peer, otherwise SBN_ERROR
 */
SBN_Status_t SBN_ProcessUnsubFromPeer(SBN_PeerRecord_t *PeerRec, CFE_SB_MsgId_t MsgID)
{
    SBN_ModuleIdx_t  FilterIdx;
    SBN_Filter_Ctx_t Filter_Context;
    SBN_Status_t     SBN_Status;

    int i;
    SBN_SubCnt_t idx = 0;

    Filter_Context.MyProcessorID    = CFE_PSP_GetProcessorId();
    Filter_Context.MySpacecraftID   = CFE_PSP_GetSpacecraftId();
    Filter_Context.PeerProcessorID  = PeerRec->Intf.ProcessorID;
    Filter_Context.PeerSpacecraftID = PeerRec->Intf.SpacecraftID;

    for (FilterIdx = 0; FilterIdx < PeerRec->Intf.FilterCnt; FilterIdx++)
    {
        if (PeerRec->Intf.Filters[FilterIdx]->RemapMID == NULL)
        {
            continue;
        } /* end if */

        SBN_Status = (PeerRec->Intf.Filters[FilterIdx]->RemapMID)(&MsgID, &Filter_Context);

        if (SBN_Status != SBN_SUCCESS)
        {
            /* assume the filter generated an event */
            return SBN_Status;
        } /* end if */
    } /* end for */

    if (!SBN_IsPeerMsgIDSub(&idx, MsgID, PeerRec))
    {
        EVSSendInfo(SBN_SUB_EID,
                    "cannot process unsubscription from ProcessorID %d, msg 0x%04X not found",
                    PeerRec->Intf.ProcessorID,
                    CFE_SB_MsgIdToValue(MsgID));
        return SBN_SUCCESS;
    } /* end if */

    /* remove sub from array for that peer and
    ** shift all subscriptions in higher elements to fill the gap
    ** note that the Subs[] array has one extra element to allow for an
    ** unsub from a full table.
    */
    for (i = idx; i < PeerRec->Intf.SubCnt; i++)
    {
        memcpy(&PeerRec->Intf.Subs[i], &PeerRec->Intf.Subs[i + 1], sizeof(SBN_Subs_t));
    } /* end for */

    /* decrement sub cnt */
    PeerRec->Intf.SubCnt--;

    OS_printf("%s(): Remove Sub=%04x total=%u\n",__func__, CFE_SB_MsgIdToValue(MsgID), (unsigned int)PeerRec->Intf.SubCnt);

    /* Track this individual change */
    SBN_Subs_RegisterChange(&PeerRec->PeerState.Q, false, MsgID, CFE_SB_DEFAULT_QOS);

    return SBN_SUCCESS;
} /* end SBN_ProcessUnsubFromPeer */

/**
 * \brief Process an unsubscription message from a peer.
 *
 * @param[in] Peer The peer interface
 * @param[in] Msg The unsubscription SBN message.
 *
 * @return SBN_SUCCESS always (whether or not there were some unsubs that failed.)
 */
SBN_Status_t SBN_ProcessUnsubsFromPeer(SBN_PeerRecord_t *PeerRec, void *Msg)
{
    SBN_UnpackState_t Pack;

    SBN_Unpack_Init(&Pack, Msg, CFE_MISSION_SB_MAX_SB_MSG_SIZE);

    char VersionHash[SBN_IDENT_LEN];

    SBN_Unpack_Data(&Pack, VersionHash, SBN_IDENT_LEN);

    if (strncmp(VersionHash, SBN_IDENT, SBN_IDENT_LEN))
    {
        EVSSendInfo(SBN_PROTO_EID, "version number mismatch with peer CpuID %d", PeerRec->Intf.ProcessorID);
    }

    uint16 SubCnt = 0;
    SBN_Unpack_UInt16(&Pack, &SubCnt);

    int SubIdx;
    for (SubIdx = 0; SubIdx < SubCnt; SubIdx++)
    {
        CFE_SB_MsgId_t MsgID = CFE_SB_INVALID_MSG_ID;
        SBN_Unpack_MsgID(&Pack, &MsgID);
        CFE_SB_Qos_t QoS = { 0 };
        SBN_Unpack_Data(&Pack, &QoS, sizeof(QoS));

        SBN_ProcessUnsubFromPeer(PeerRec, MsgID); /* ignore return value, I want to unsub as much as I can */
    } /* end for */

    return SBN_SUCCESS;
} /* end SBN_ProcessUnsubsFromPeer() */

/**
 * When SBN starts, it queries for all existing subscriptions. This method
 * processes those subscriptions.
 *
 * @param[in] Ptr SB message pointer.
 *
 * @return SBN_SUCCESS on successful subscriptions, otherwise SBN_ERROR.
 */
SBN_Status_t SBN_ProcessAllSubscriptions(CFE_SB_AllSubscriptionsTlm_t *Ptr)
{
    SBN_Status_t SBN_Status = SBN_SUCCESS;
    int          i;

    if (Ptr->Payload.Entries > CFE_MISSION_SB_SUB_ENTRIES_PER_PKT)
    {
        EVSSendErr(SBN_SUB_EID,
                   "entries value %d in SB PrevSubMsg exceeds max %d, aborting",
                   (int)Ptr->Payload.Entries,
                   CFE_MISSION_SB_SUB_ENTRIES_PER_PKT);
        return SBN_ERROR;
    } /* end if */

    EVSSendInfo(SBN_SUB_EID, "Processing all subscriptions...");
    SBN_Subs_InvalidateChangeQ(&SBN_AppData.SubChangeQ);

    for (i = 0; i < Ptr->Payload.Entries; i++)
    {
        SBN_Status = ProcessLocalSub(Ptr->Payload.Entry[i].MsgId, Ptr->Payload.Entry[i].Qos);
        if (SBN_Status != SBN_SUCCESS)
        {
            return SBN_Status;
        } /* end if */
    } /* end for */

    return SBN_Status;
} /* end SBN_ProcessAllSubscriptions */

/**
 * Removes all subscriptions (unsubscribe from the local SB )
 * for the specified peer, particularly when the peer connection has been lost.
 *
 * @param[in] Peer The peer interface
 *
 * @return SBN_SUCCESS always (whether or not there were some unsubs that failed.)
 */
SBN_Status_t SBN_RemoveAllSubsFromPeer(SBN_PeerRecord_t *PeerRec)
{
    int          i;
    CFE_Status_t CFE_Status;

    if (CFE_RESOURCEID_TEST_DEFINED(PeerRec->Intf.Pipe))
    {
        for (i = 0; i < PeerRec->Intf.SubCnt; i++)
        {
            /** Ignore CFE_SB_BAD_ARGUMENT errors -- currently not checking if the pipe is valid before unsubscribing **/
            CFE_Status = CFE_SB_UnsubscribeLocal(PeerRec->Intf.Subs[i].MsgID, PeerRec->Intf.Pipe);
            if (CFE_Status != CFE_SUCCESS && CFE_Status != CFE_SB_BAD_ARGUMENT)
            {
                EVSSendErr(SBN_SUB_EID,
                        "unable to unsubscribe from message id 0x%04X: 0x%08X",
                        CFE_SB_MsgIdToValue(PeerRec->Intf.Subs[i].MsgID),
                        CFE_Status);
                /* but continue processing... */
            } /* end if */
        } /* end for */

        EVSSendInfo(SBN_SUB_EID, "unsubscribed %d message id's from ProcessorID %d", (int)PeerRec->Intf.SubCnt, PeerRec->Intf.ProcessorID);
    }

    PeerRec->PeerState.LocalPipeSubCnt = 0;

    return SBN_SUCCESS;
} /* end SBN_RemoveAllSubsFromPeer */

SBN_Status_t SBN_Subs_SendUpdateToPeer(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    SBN_SubChange_t *ChangePtr;
    SBN_Status_t SBN_Status;
    SBN_MsgType_t MsgType;

    ChangePtr = SBN_Subs_GetPrevChangeRec(&SBN_AppData.SubChangeQ, PeerRec->PeerState.PeerSentSubVersion);
    if (ChangePtr == NULL)
    {
        SBN_Status = SBN_ERROR;
    }
    else
    {
        if (ChangePtr->IsSubscribe)
        {
            MsgType = SBN_SUB_MSG;
        }
        else
        {
            MsgType = SBN_UNSUB_MSG;
        }

        SBN_Status = SBN_SendLocalSubToPeer(MsgType, ChangePtr->MsgId, ChangePtr->Qos, &PeerRec->Intf);

        if (SBN_Status == SBN_SUCCESS)
        {
            /* incremental update worked */
            ++PeerRec->PeerState.PeerSentSubVersion;
        }
    }

    return SBN_Status;
}

SBN_Status_t SBN_Subs_ProcessUpdateFromPeer(SBN_PeerRecord_t *PeerRec)
{
    SBN_SubChange_t *ChangePtr;
    SBN_Status_t SBN_Status;
    CFE_Status_t CFE_Status;

    SBN_Status = SBN_ERROR;
    ChangePtr = SBN_Subs_GetPrevChangeRec(&PeerRec->PeerState.Q, PeerRec->PeerState.LocalPipeSubVersion);
    if (ChangePtr != NULL)
    {
        if (ChangePtr->IsSubscribe)
        {
            CFE_Status = CFE_SB_SubscribeLocal(ChangePtr->MsgId,
                                        PeerRec->Intf.Pipe,
                                        SBN_PLATFORM_PEER_MSG_LIM);
        }
        else
        {
            CFE_Status = CFE_SB_UnsubscribeLocal(ChangePtr->MsgId,
                                        PeerRec->Intf.Pipe);
        }

        if (CFE_Status == CFE_SUCCESS)
        {
            /* incremental update worked */
            ++PeerRec->PeerState.LocalPipeSubVersion;

            if (ChangePtr->IsSubscribe)
            {
                ++PeerRec->PeerState.LocalPipeSubCnt;
            }
            else if (PeerRec->PeerState.LocalPipeSubCnt > 0)
            {
                --PeerRec->PeerState.LocalPipeSubCnt;
            }

            SBN_Status = SBN_SUCCESS;
        }
    }

    return SBN_Status;
}

SBN_Status_t SBN_ProcessAllSubsFromPeer(SBN_PeerRecord_t *PeerRec)
{
    uint32 SubsVersion;
    CFE_Status_t CFE_Status;
    SBN_Status_t SBN_Status;
    SBN_SubCnt_t i;

    SubsVersion = PeerRec->PeerState.Q.CurrVersion;
    SBN_Status = SBN_SUCCESS;
    for (i=0; i < PeerRec->Intf.SubCnt; ++i)
    {
        CFE_Status = CFE_SB_SubscribeLocal(PeerRec->Intf.Subs[i].MsgID,
                                        PeerRec->Intf.Pipe,
                                        SBN_PLATFORM_PEER_MSG_LIM);

        if (CFE_Status == CFE_SUCCESS)
        {
            ++PeerRec->PeerState.LocalPipeSubCnt;
        }
        else
        {
            SBN_Status = SBN_ERROR;
            break;
        }
    }

    if (SBN_Status == SBN_SUCCESS)
    {
        /* Record that we sent this version of the subs table to the peer */
        PeerRec->PeerState.LocalPipeSubVersion = SubsVersion;
    }

    return SBN_Status;

}

void SBN_Subs_UpdatePeer(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D)
{
    SBN_Status_t SBN_Status;
    uint32 LastVersion;


    while (PeerRec->PeerState.PeerSentSubVersion != SBN_AppData.SubChangeQ.CurrVersion)
    {
    OS_printf("%s(): SendToPeer loop start, PeerSent=%u LocalVersion=%u\n", __func__, PeerRec->PeerState.PeerSentSubVersion, SBN_AppData.SubChangeQ.CurrVersion);
        LastVersion = PeerRec->PeerState.PeerSentSubVersion;
        SBN_Status = SBN_Subs_SendUpdateToPeer(PeerRec, D);
        if (SBN_Status != SBN_SUCCESS)
        {
            /* single update not possible, do full update */
            SBN_Status = SBN_SendLocalSubsToPeer(PeerRec, D);
        }

        /* Note - if either of the above functions worked, it should have updated PeerSentSubVersion */
        if (PeerRec->PeerState.PeerSentSubVersion == LastVersion || SBN_Status != SBN_SUCCESS)
        {
            /* no update was possible, something is broken, do not try endlessly */
            break;
        }
    }

    OS_printf("%s(): SendToPeer loop complete, PeerSent=%u LocalVersion=%u\n", __func__, PeerRec->PeerState.PeerSentSubVersion, SBN_AppData.SubChangeQ.CurrVersion);

    while (PeerRec->PeerState.LocalPipeSubVersion != PeerRec->PeerState.Q.CurrVersion)
    {
    OS_printf("%s(): LocalSub loop start, LocalPipeSubVersion=%u CurrVersion=%u\n", __func__, PeerRec->PeerState.LocalPipeSubVersion, PeerRec->PeerState.Q.CurrVersion);
        LastVersion = PeerRec->PeerState.LocalPipeSubVersion;
        SBN_Status = SBN_Subs_ProcessUpdateFromPeer(PeerRec);
        if (SBN_Status != SBN_SUCCESS)
        {
            if (PeerRec->PeerState.LocalPipeSubCnt == 0)
            {
                /* This means the pipe is clean and we can just subscribe based on the list from the peer */
                SBN_Status = SBN_ProcessAllSubsFromPeer(PeerRec);
            }
            else if (!PeerRec->State.OutboundDataIsScheduled)
            {
                /* single update not possible.  This means the peer is way out of sync. */
                OS_printf("%s(): Purge and resubscribe!\n",__func__);

                /* The only way to fix this is to delete and recreate the pipe.  This may be extreme
                * but SBN does not have an API to purge existing subscriptions so we can refresh them.
                * Deleting the pipe will implicitly unsubscribe everything, but it changes the pipe ID. */
                CFE_SB_DeletePipe(PeerRec->Intf.Pipe);

                /* When the main task sees this pipe ID unset it will re-create the pipe automatically */
                PeerRec->Intf.Pipe = CFE_SB_INVALID_PIPE;
            }
        }

        /* Note - if either of the above functions worked, it should have updated PeerSentSubVersion */
        if (PeerRec->PeerState.LocalPipeSubVersion == LastVersion || SBN_Status != SBN_SUCCESS)
        {
            /* no update was possible, something is broken, do not try endlessly */
            break;
        }
    }

    OS_printf("%s(): LocalSub loop complete, LocalPipeSubVersion=%u CurrVersion=%u\n", __func__, PeerRec->PeerState.LocalPipeSubVersion, PeerRec->PeerState.Q.CurrVersion);
}
