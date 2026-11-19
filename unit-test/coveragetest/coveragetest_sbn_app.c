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
#include "cfe_msgids.h"
#include "cfe_sb_eventids.h"
#include "cfe_evs_msg.h"
#include "sbn_pack.h"
#include "sbn_error.h"

//jphfix static const CFE_SB_MsgId_t SBN_UT_MsgID = CFE_SB_MSGID_WRAP_VALUE(0x1818);
static SBN_ConfTbl_t        SBN_UT_TestConfTbl;

/********************************** tests ************************************/
#ifdef jphfix
static void SBN_UT_AppMain_EVSRegisterErr(void)
{
    START();

    UT_SetDeferredRetcode(UT_KEY(CFE_EVS_Register), 1, -1);

    SBN_AppMain();

    UtAssert_STUB_COUNT(CFE_ES_GetAppID, 0);
} /* end AppMain_EVSRegisterErr() */

static void SBN_UT_AppMain_AppIdErr(void)
{
    START();

    UT_CheckEvent_Setup(SBN_INIT_EID, "ERROR: could not start SBN: unable to get AppID");
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_GetAppID), 1, -1);

    SBN_AppMain();

    UtAssert_STUB_COUNT(OS_TaskGetId, 0);
    EVENT_CNT(1);
} /* end AppMain_AppIdErr() */

static void SBN_UT_AppMain_TaskInfoErr(void)
{
    START();

    UT_CheckEvent_Setup(SBN_INIT_EID, "ERROR: could not start SBN: SBN failed to get task info (");
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_GetTaskInfo), 1, -1);

    SBN_AppMain();

    EVENT_CNT(1);
} /* end AppMain_TaskInfoErr() */
#endif
void SBN_UT_Test_AppMain(void)
{
    START();

    /* Test Case for:
     * void SBN_AppMain(void);
     */

    UtAssert_VOIDCALL(SBN_AppMain());
}

void SBN_UT_Test_GetPeerRecord(void)
{
    START();

    /* Test Case for:
     * SBN_PeerRecord_t *SBN_GetPeerRecord(SBN_NetRecord_t *NetRec, SBN_ProcessorID_t ProcessorID, SBN_SpacecraftID_t
     * SpacecraftID);
     */
    SBN_NetRecord_t *NetRec = &SBN_AppData.NetRecords[0];
    SBN_ProcessorID_t ProcessorID = SBN_UT_PEER_PROCESSOR_ID;
    SBN_SpacecraftID_t SpacecraftID = SBN_UT_SPACECRAFT_ID;

     UtAssert_NOT_NULL(SBN_GetPeerRecord(NetRec, ProcessorID, SpacecraftID));
}

void SBN_UT_Test_ReloadConfTbl(void)
{
    START();

    /* Test Case for:
     * SBN_Status_t SBN_ReloadConfTbl(void);
     */
    UtAssert_INT32_EQ(SBN_ReloadConfTbl(), SBN_SUCCESS);
}

void SBN_UT_Test_InitializePeerCounters(void)
{
    START();

    /* Test Case for:
     * void SBN_InitializePeerCounters(SBN_PeerRecord_t *PeerRec);
     */
    SBN_PeerRecord_t *PeerRec = &SBN_AppData.PeerRecords[0];

    UtAssert_VOIDCALL(SBN_InitializePeerCounters(PeerRec));
}

void SBN_UT_Test_InitializeCounters(void)
{
    START();

    /* Test Case for:
     * void SBN_InitializeCounters(void);
     */
     UtAssert_VOIDCALL(SBN_InitializeCounters());
}

void SBN_UT_Test_ValidateConfTbl(void)
{
    START();

    /* Test Case for:
     * SBN_Status_t SBN_ValidateConfTbl(void *TableData);
     */
    void *TableData = &SBN_UT_TestConfTbl;

    UtAssert_INT32_EQ(SBN_ValidateConfTbl(TableData), SBN_SUCCESS);
}

void SBN_UT_Test_GetPeer(void)
{
    START();

    /* Test Case for:
     * SBN_PeerInterface_t *SBN_GetPeer(SBN_NetInterface_t *Net, SBN_ProcessorID_t ProcessorID, SBN_SpacecraftID_t
     * SpacecraftID);
     */
    SBN_NetInterface_t *Net = &SBN_AppData.NetRecords[0].Intf;
    SBN_ProcessorID_t ProcessorID = SBN_UT_PEER_PROCESSOR_ID;
    SBN_SpacecraftID_t SpacecraftID = SBN_UT_SPACECRAFT_ID;

    UtAssert_NOT_NULL(SBN_GetPeer(Net, ProcessorID, SpacecraftID));
}

void SBN_UT_Test_PackMsg(void)
{
    START();

    /* Test Case for:
     * void SBN_PackMsg(void *SBNBuf, SBN_MsgSz_t MsgSz, SBN_MsgType_t MsgType, SBN_ProcessorID_t ProcessorID,
     * SBN_SpacecraftID_t SpacecraftID, void *Msg);
     */
    void *SBNBuf = NULL;
    SBN_MsgSz_t MsgSz = 0;
    SBN_MsgType_t MsgType = SBN_NO_MSG;
    SBN_ProcessorID_t ProcessorID = SBN_UT_PEER_PROCESSOR_ID;
    SBN_SpacecraftID_t SpacecraftID = SBN_UT_SPACECRAFT_ID;
    void *Msg = NULL;

    UtAssert_VOIDCALL(SBN_PackMsg(SBNBuf, MsgSz, MsgType, ProcessorID, SpacecraftID, Msg));
}

void SBN_UT_Test_UnpackMsg(void)
{
    START();

    /* Test Case for:
     * bool SBN_UnpackMsg(void *SBNBuf, SBN_MsgSz_t *MsgSzPtr, SBN_MsgType_t *MsgTypePtr, SBN_ProcessorID_t
     * *ProcessorIDPtr, SBN_SpacecraftID_t *SpacecraftIDPtr, void *Msg);
     */
    void *SBNBuf = NULL;
    SBN_MsgSz_t MsgSz;
    SBN_MsgType_t MsgType;
    SBN_ProcessorID_t ProcessorID;
    SBN_SpacecraftID_t SpacecraftID;
    void *Msg = NULL;

    UtAssert_VOIDCALL(SBN_UnpackMsg(SBNBuf, &MsgSz, &MsgType, &ProcessorID, &SpacecraftID, Msg));
}

void SBN_UT_Test_SendNetMsg(void)
{
    START();

    /* Test Case for:
     * SBN_Status_t SBN_SendNetMsg(SBN_MsgType_t MsgType, SBN_MsgSz_t MsgSz, void *Msg, SBN_PeerInterface_t *Peer);
     */
    SBN_MsgType_t MsgType = SBN_NO_MSG;
    SBN_MsgSz_t MsgSz = 0;
    void *Msg = NULL;
    SBN_PeerInterface_t *Peer = &SBN_AppData.PeerRecords[0].Intf;

    UtAssert_INT32_EQ(SBN_SendNetMsg(MsgType, MsgSz, Msg, Peer), SBN_SUCCESS);
}

void UT_Setup(void)
{
} /* end UT_Setup() */

void UT_TearDown(void)
{
} /* end UT_TearDown() */

void UtTest_Setup(void)
{
    UtTest_Add(SBN_UT_Test_AppMain, UT_Setup, UT_TearDown, "SBN_AppMain");
    UtTest_Add(SBN_UT_Test_GetPeerRecord, UT_Setup, UT_TearDown, "SBN_GetPeerRecord");
    UtTest_Add(SBN_UT_Test_ReloadConfTbl, UT_Setup, UT_TearDown, "SBN_ReloadConfTbl");
    UtTest_Add(SBN_UT_Test_InitializePeerCounters, UT_Setup, UT_TearDown, "SBN_InitializePeerCounters");
    UtTest_Add(SBN_UT_Test_InitializeCounters, UT_Setup, UT_TearDown, "SBN_InitializeCounters");
    UtTest_Add(SBN_UT_Test_ValidateConfTbl, UT_Setup, UT_TearDown, "SBN_ValidateConfTbl");
    UtTest_Add(SBN_UT_Test_GetPeer, UT_Setup, UT_TearDown, "SBN_GetPeer");
    UtTest_Add(SBN_UT_Test_PackMsg, UT_Setup, UT_TearDown, "SBN_PackMsg");
    UtTest_Add(SBN_UT_Test_UnpackMsg, UT_Setup, UT_TearDown, "SBN_UnpackMsg");
    UtTest_Add(SBN_UT_Test_SendNetMsg, UT_Setup, UT_TearDown, "SBN_SendNetMsg");
}
