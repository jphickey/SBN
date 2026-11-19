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

#include "sbn_coveragetest_common.h"
#include "sbn_app.h"
#include "sbn_encode.h"
#include "sbn_dispatch.h"

union
{
    CFE_SB_Buffer_t         Buf;
    CFE_MSG_CommandHeader_t CommandHeader;

    SBN_NoopCmd_t           NoopCmd;
    SBN_SendHkCmd_t         SendHkCmd;
    SBN_SendHkNetCmd_t      SendHkNetCmd;
    SBN_SendHkPeerCmd_t     SendHkPeerCmd;
    SBN_SendHkPeerSubsCmd_t SendHkPeerSubsCmd;
    SBN_SendHkMySubsCmd_t   SendHkMySubsCmd;
    SBN_ResetCmd_t          ResetCmd;
    SBN_ResetPeerCmd_t      ResetPeerCmd;
    SBN_WakeupCmd_t         WakeupCmd;
    SBN_ReloadTblCmd_t      ReloadTblCmd;

} Buffer;

union
{
    SBN_HKTlm_t HkTlm;
    SBN_HkNetTlm_t HkNetTlm;
    SBN_HkPeerSubsTlm_t HkPeerSubsTlm;
    SBN_HkMySubsTlm_t HkMySubsTlm;
} SBN_UT_TlmBuf;

CFE_MSG_Message_t *CmdPktPtr = &Buffer.Buf.Msg;
CFE_MSG_Size_t     MsgSz     = sizeof(CFE_MSG_CommandHeader_t);
CFE_MSG_FcnCode_t  FcnCode   = SBN_NOOP_CC;

static void UT_SBN_MsgInitHelper(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_ValueToMsgId(SBN_CMD_MID);

    CFE_MSG_Init(CmdPktPtr, MsgId, MsgSz);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &MsgId, sizeof(MsgId), true);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &MsgSz, sizeof(MsgSz), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
}

#define MSGINIT(x) UT_SBN_MsgInitHelper()

static void NOOP_Nominal(void)
{
    START();


    UT_CheckEvent_Setup(SBN_CMD_EID, "no-op command");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(SBN_NoopCmd_t);
    FcnCode = SBN_NOOP_CC;
    MSGINIT();

    SBN_NoopCmd(&Buffer.NoopCmd);

    EVENT_CNT(1);
} /* end NOOP_Nominal() */

static void HKNet_NetIdErr(void)
{
    START();

    UT_ResetState(UT_KEY(SBN_LocateNetRecordByExtIdx));

    UT_CheckEvent_Setup(SBN_CMD_EID, "Invalid NetIdx ");

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.SendHkNetCmd.Payload.NetIdx = 255;

    MsgSz   = sizeof(SBN_SendHkNetCmd_t);
    FcnCode = SBN_HK_NET_CC;
    MSGINIT();

    SBN_SendHkNetCmd(&Buffer.SendHkNetCmd);

    EVENT_CNT(1);
} /* end HKNet_NetIdErr() */

static void HKNet_Nominal(void)
{
    START();

    UT_SetDefaultReturnValue(UT_KEY(SBN_Codec_GetTlmBuffer), (UT_IntReturn_t) &SBN_UT_TlmBuf);
    UT_CheckEvent_Setup(SBN_CMD_EID, "hk command, net=");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(SBN_SendHkNetCmd_t);
    FcnCode = SBN_HK_NET_CC;
    MSGINIT();

    SBN_SendHkNetCmd(&Buffer.SendHkNetCmd);

    EVENT_CNT(1);
} /* end HKNet_Nominal() */

static void HKPeer_NetIdErr(void)
{
    START();

    UT_ResetState(UT_KEY(SBN_LocateNetRecordByExtIdx));

    UT_CheckEvent_Setup(SBN_CMD_EID, "Invalid NetIdx ");

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.SendHkNetCmd.Payload.NetIdx = 255;

    MsgSz   = sizeof(SBN_SendHkPeerCmd_t);
    FcnCode = SBN_HK_PEER_CC;
    MSGINIT();

    SBN_SendHkPeerCmd(&Buffer.SendHkPeerCmd);

    EVENT_CNT(1);
} /* end HKPeer_NetIdErr() */

static void HKPeer_PeerIdErr(void)
{
    START();

    UT_ResetState(UT_KEY(SBN_LocatePeerRecordInNet));

    UT_CheckEvent_Setup(SBN_CMD_EID, "Invalid PeerIdx ");

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.SendHkPeerCmd.Payload.PeerIdx = 255;

    MsgSz   = sizeof(SBN_SendHkPeerCmd_t);
    FcnCode = SBN_HK_PEER_CC;
    MSGINIT();

    SBN_SendHkPeerCmd(&Buffer.SendHkPeerCmd);

    EVENT_CNT(1);
} /* end HKPeer_PeerIdErr() */

static void HKPeer_Nominal(void)
{
    START();

    UT_SetDefaultReturnValue(UT_KEY(SBN_Codec_GetTlmBuffer), (UT_IntReturn_t) &SBN_UT_TlmBuf);

    UT_CheckEvent_Setup(SBN_CMD_EID, "hk peer command, net=");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(SBN_SendHkPeerCmd_t);
    FcnCode = SBN_HK_PEER_CC;
    MSGINIT();

    SBN_SendHkPeerCmd(&Buffer.SendHkPeerCmd);

    EVENT_CNT(1);
} /* end HKPeer_Nominal() */

static void HKPeerSubs_NetIdErr(void)
{
    START();

    UT_ResetState(UT_KEY(SBN_LocateNetRecordByExtIdx));

    UT_CheckEvent_Setup(SBN_CMD_EID, "Invalid NetIdx ");

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.SendHkPeerSubsCmd.Payload.NetIdx = 255;

    MsgSz   = sizeof(SBN_SendHkPeerSubsCmd_t);
    FcnCode = SBN_HK_PEERSUBS_CC;
    MSGINIT();

    SBN_SendHkPeerSubsCmd(&Buffer.SendHkPeerSubsCmd);

    EVENT_CNT(1);
} /* end HKPeerSubs_NetIdErr() */

static void HKPeerSubs_PeerIdErr(void)
{
    START();

    UT_ResetState(UT_KEY(SBN_LocatePeerRecordInNet));

    UT_CheckEvent_Setup(SBN_CMD_EID, "Invalid PeerIdx ");

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.SendHkPeerSubsCmd.Payload.PeerIdx = 255;

    MsgSz   = sizeof(SBN_SendHkPeerSubsCmd_t);
    FcnCode = SBN_HK_PEERSUBS_CC;
    MSGINIT();

    SBN_SendHkPeerSubsCmd(&Buffer.SendHkPeerSubsCmd);

    EVENT_CNT(1);
} /* end HKPeerSubs_PeerIdErr() */

static void HKPeerSubs_Nominal(void)
{
    START();

    UT_SetDefaultReturnValue(UT_KEY(SBN_Codec_GetTlmBuffer), (UT_IntReturn_t) &SBN_UT_TlmBuf);

    UT_CheckEvent_Setup(SBN_CMD_EID, "hk subs command, net=");

    memset(&Buffer, 0, sizeof(Buffer));

    PeerPtr->SubSet.SubCnt = 1;

    MsgSz   = sizeof(SBN_SendHkPeerSubsCmd_t);
    FcnCode = SBN_HK_PEERSUBS_CC;
    MSGINIT();

    SBN_SendHkPeerSubsCmd(&Buffer.SendHkPeerSubsCmd);

    EVENT_CNT(1);
} /* end HKPeerSubs_Nominal() */

static void HKMySubs_Nominal(void)
{
    START();

    UT_SetDefaultReturnValue(UT_KEY(SBN_Codec_GetTlmBuffer), (UT_IntReturn_t) &SBN_UT_TlmBuf);

    UT_CheckEvent_Setup(SBN_CMD_EID, "hk subs command");

    memset(&Buffer, 0, sizeof(Buffer));

    SBN_AppData.SubSet.SubCnt = 1;

    MsgSz   = sizeof(SBN_SendHkMySubsCmd_t);
    FcnCode = SBN_HK_MYSUBS_CC;
    MSGINIT();

    SBN_SendHkMySubsCmd(&Buffer.SendHkMySubsCmd);

    EVENT_CNT(1);
} /* end HKMySubs_Nominal() */

static void HKReset_Nominal(void)
{
    START();


    UT_CheckEvent_Setup(SBN_CMD_EID, "reset command");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(CFE_MSG_CommandHeader_t);
    FcnCode = SBN_HK_RESET_CC;
    MSGINIT();

    SBN_ResetCmd(&Buffer.ResetCmd);

    EVENT_CNT(1);
} /* end HKReset_Nominal() */

static void HKResetPeer_NetIdErr(void)
{
    START();

    UT_ResetState(UT_KEY(SBN_LocateNetRecordByExtIdx));

    UT_CheckEvent_Setup(SBN_CMD_EID, "Invalid NetIdx ");

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.ResetPeerCmd.Payload.NetIdx = 255;

    MsgSz   = sizeof(SBN_ResetPeerCmd_t);
    FcnCode = SBN_HK_RESET_PEER_CC;
    MSGINIT();

    SBN_ResetPeerCmd(&Buffer.ResetPeerCmd);

    EVENT_CNT(1);
} /* end HKResetPeer_NetIdErr() */

static void HKResetPeer_PeerIdErr(void)
{
    START();

    UT_ResetState(UT_KEY(SBN_LocatePeerRecordInNet));

    UT_CheckEvent_Setup(SBN_CMD_EID, "Invalid PeerIdx ");

    memset(&Buffer, 0, sizeof(Buffer));
    Buffer.ResetPeerCmd.Payload.PeerIdx = 255;

    MsgSz   = sizeof(SBN_ResetPeerCmd_t);
    FcnCode = SBN_HK_RESET_PEER_CC;
    MSGINIT();

    SBN_ResetPeerCmd(&Buffer.ResetPeerCmd);

    EVENT_CNT(1);
} /* end HKResetPeer_PeerIdErr() */

static void HKResetPeer_Nominal(void)
{
    START();


    UT_CheckEvent_Setup(SBN_CMD_EID, "hk reset peer command (NetIdx=");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(SBN_ResetPeerCmd_t);
    FcnCode = SBN_HK_RESET_PEER_CC;
    MSGINIT();

    SBN_ResetPeerCmd(&Buffer.ResetPeerCmd);

    EVENT_CNT(1);
} /* end HKResetPeer_Nominal() */

static void SCH_Nominal(void)
{
    START();


    UT_CheckEvent_Setup(SBN_CMD_EID, "wakeup");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(SBN_WakeupCmd_t);
    FcnCode = SBN_SCH_WAKEUP_CC;
    MSGINIT();

    SBN_WakeupCmd(&Buffer.WakeupCmd);

    EVENT_CNT(1);
} /* end SCH_Nominal() */


static void TBL_Nominal(void)
{
    START();


    UT_CheckEvent_Setup(SBN_CMD_EID, "reload tbl command");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(SBN_ReloadTblCmd_t);
    FcnCode = SBN_TBL_CC;
    MSGINIT();

    SBN_ReloadTblCmd(&Buffer.ReloadTblCmd);

    EVENT_CNT(1);
} /* end TBL_Nominal() */

static void HK_Nominal(void)
{
    START();


    UT_CheckEvent_Setup(SBN_CMD_EID, "hk command");

    memset(&Buffer, 0, sizeof(Buffer));

    MsgSz   = sizeof(CFE_MSG_CommandHeader_t);
    FcnCode = SBN_HK_CC;
    MSGINIT();

    SBN_SendHkCmd(&Buffer.SendHkCmd);

    EVENT_CNT(1);
} /* end HK_Nominal() */

static void Test_SBN_Cmds(void)
{
    NOOP_Nominal();
    HKNet_NetIdErr();
    HKNet_Nominal();
    HKPeer_NetIdErr();
    HKPeer_PeerIdErr();
    HKPeer_Nominal();
    HKPeerSubs_NetIdErr();
    HKPeerSubs_PeerIdErr();
    HKPeerSubs_Nominal();
    HKMySubs_Nominal();
    HKReset_Nominal();
    HKResetPeer_NetIdErr();
    HKResetPeer_PeerIdErr();
    HKResetPeer_Nominal();
    SCH_Nominal();
    TBL_Nominal();
    HK_Nominal();
}

void UT_Setup(void)
{
} /* end UT_Setup() */

void UT_TearDown(void)
{
} /* end UT_TearDown() */

void UtTest_Setup(void)
{
    ADD_TEST(SBN_Cmds);
}
