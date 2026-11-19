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
#include "sbn_inbound.h"
#include "sbn_error.h"
#include "sbn_filter.h"

static void ProcessPeerMsg_AppMsg_FiltErr(void)
{
    START();

    /* Configuration Setup Rationale:
     * Filter Setup:
     * - FilterCnt: 1 - Single filter to test error condition
     * - Filters[0]: Filter with RecvFilter_Err (returns SBN_ERROR) - Triggers filter error during message processing
     * - ProcessorID/SpacecraftID: Set to match the message being processed to ensure peer lookup succeeds */
    UT_ResetState(0);
    SBN_BufferDesc_t      BufD;

    memset(&BufD, 0, sizeof(BufD));

    PeerPtr->FilterCnt  = 1;

    UT_SetDefaultReturnValue(UT_KEY(CFE_PSP_GetProcessorId), ProcessorID);
    UT_SetDefaultReturnValue(UT_KEY(CFE_PSP_GetSpacecraftId), SpacecraftID);
    UT_SetDeferredRetcode(UT_KEY(SBN_Filter_Mutation), 1, -1);

    BufD.MsgType = SBN_APP_MSG;

    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_ERROR);
} /* end ProcessPeerMsg_AppMsg_FiltErr() */

static void ProcessPeerMsg_AppMsg_FiltOut(void)
{
    START();

    /* Configuration Setup Rationale:
     * Filter Setup:
     * - FilterCnt: 1 - Single filter to test message filtering
     * - Filters[0]: Filter with RecvFilter_Out (returns SBN_IF_EMPTY) - Filters out the message
     * - PeerPtr->ProcessorID/SpacecraftID: Set to match message parameters to ensure peer lookup succeeds
     * - ProcessorID/SpacecraftID: Set to match the message being processed */
    UT_ResetState(0);
    SBN_BufferDesc_t      BufD;

    memset(&BufD, 0, sizeof(BufD));

    PeerPtr->ProcessorID  = ProcessorID;
    PeerPtr->SpacecraftID = SpacecraftID;
    PeerPtr->FilterCnt    = 1;

    UT_SetDefaultReturnValue(UT_KEY(CFE_PSP_GetProcessorId), ProcessorID);
    UT_SetDefaultReturnValue(UT_KEY(CFE_PSP_GetSpacecraftId), SpacecraftID);
    UT_SetDeferredRetcode(UT_KEY(SBN_Filter_Mutation), 1, SBN_IF_EMPTY);

    BufD.MsgType = SBN_APP_MSG;

    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_IF_EMPTY);
} /* end ProcessPeerMsg_AppMsg_FiltOut() */

static SBN_Status_t RecvFilter_Nominal(void *Data, SBN_Filter_Ctx_t *CtxPtr)
{
    return SBN_SUCCESS;
} /* end RecvFilter_Nominal() */

static void ProcessPeerMsg_AppMsg_PassMsgErr(void)
{
    START();
    SBN_BufferDesc_t BufD;

    memset(&BufD, 0, sizeof(BufD));

    UT_ResetState(0);
    UT_CheckEvent_Setup(SBN_SB_EID, "CFE_SB_TransmitMsg error");

    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetProcessorId), 1, ProcessorID);
    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetSpacecraftId), 1, SpacecraftID);

    UT_SetDeferredRetcode(UT_KEY(CFE_SB_TransmitMsg), 1, -1);

    BufD.MsgType = SBN_APP_MSG;

    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_ERROR);

    EVENT_CNT(1);
} /* end ProcessPeerMsg_AppMsg_PassMsgErr() */

static void ProcessPeerMsg_AppMsg_Nominal(void)
{
    START();

    /* Configuration Setup Rationale:
     * Filter Setup:
     * - FilterCnt: 2 - Two filters to test filter chain processing
     * - Filters[0]: Filter_Empty (NULL FilterRecv function) - Should be skipped during processing
     * - Filters[1]: Filter_Nominal (returns SBN_SUCCESS) - Allows message to pass through
     * - ProcessorID/SpacecraftID: Set to match the message being processed to ensure peer lookup succeeds */
    UT_ResetState(0);
    SBN_FilterInterface_t Filter_Empty, Filter_Nominal;
    SBN_BufferDesc_t      BufD;

    memset(&BufD, 0, sizeof(BufD));

    memset(&Filter_Empty, 0, sizeof(Filter_Empty));
    memset(&Filter_Nominal, 0, sizeof(Filter_Nominal));
    Filter_Nominal.FilterRecv = RecvFilter_Nominal;

    /* Filters[0].Recv is NULL, should skip */
    PeerPtr->Filters[0] = &Filter_Empty;
    PeerPtr->Filters[1] = &Filter_Nominal;
    PeerPtr->FilterCnt  = 2;

    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetProcessorId), 1, ProcessorID);
    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetSpacecraftId), 1, SpacecraftID);

    BufD.MsgType = SBN_APP_MSG;

    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_SUCCESS);
} /* end ProcessPeerMsg_AppMsg_Nominal() */

static void ProcessPeerMsg_SubMsg_Nominal(void)
{
#ifdef jphfix
    START();

    UT_ResetState(0);
    uint8            Buf[SBN_PACKED_SUB_SZ];
    SBN_PackState_t  Pack;
    SBN_BufferDesc_t BufD;
    uint32           StartVersion;

    StartVersion = PeerRec->Priv.SubQ.CurrVersion;

    memset(&BufD, 0, sizeof(BufD));

    SBN_Pack_Init(&Pack, &Buf, SBN_PACKED_SUB_SZ, false);
    SBN_Pack_Data(&Pack, (void *)SBN_IDENT, SBN_IDENT_LEN);
    SBN_Pack_UInt16(&Pack, 1);
    SBN_Pack_MsgID(&Pack, MsgID);
    CFE_SB_Qos_t QoS = { 0 };
    SBN_Pack_Data(&Pack, (void *)&QoS, sizeof(QoS));

    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetProcessorId), 1, ProcessorID);
    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetSpacecraftId), 1, SpacecraftID);

    BufD.MsgType      = SBN_SUB_MSG;
    BufD.ActualDataSz = sizeof(Buf);
    BufD.MaxDataSz    = BufD.ActualDataSz;
    BufD.DataPtr      = Buf;

    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_SUCCESS);

    UtAssert_UINT32_EQ(PeerRec->Priv.SubQ.CurrVersion, StartVersion + 1);
#endif
} /* end ProcessPeerMsg_SubMsg_Nominal() */

static void ProcessPeerMsg_UnSubMsg_Nominal(void)
{
#ifdef jphfix
    START();

    UT_ResetState(0);
    uint8            Buf[SBN_PACKED_SUB_SZ];
    SBN_PackState_t  Pack;
    SBN_BufferDesc_t BufD;
    uint32           StartVersion;

    StartVersion = PeerRec->Priv.SubQ.CurrVersion;

    memset(&BufD, 0, sizeof(BufD));

    SBN_Pack_Init(&Pack, &Buf, sizeof(Buf), false);
    SBN_Pack_Data(&Pack, (void *)SBN_IDENT, SBN_IDENT_LEN);
    SBN_Pack_UInt16(&Pack, 1);

    SBN_Pack_MsgID(&Pack, MsgID);
    CFE_SB_Qos_t QoS;
    SBN_Pack_Data(&Pack, &QoS, sizeof(QoS));

    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetProcessorId), 1, ProcessorID);
    UT_SetDeferredRetcode(UT_KEY(CFE_PSP_GetSpacecraftId), 1, SpacecraftID);

    BufD.MsgType      = SBN_SUB_MSG;
    BufD.ActualDataSz = sizeof(Buf);
    BufD.MaxDataSz    = BufD.ActualDataSz;
    BufD.DataPtr      = Buf;
    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_SUCCESS);
    UtAssert_UINT32_EQ(PeerRec->Priv.SubQ.CurrVersion, StartVersion + 1);

    BufD.MsgType      = SBN_UNSUB_MSG;
    BufD.ActualDataSz = sizeof(Buf);
    BufD.MaxDataSz    = BufD.ActualDataSz;
    BufD.DataPtr      = Buf;
    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_SUCCESS);
    UtAssert_UINT32_EQ(PeerRec->Priv.SubQ.CurrVersion, StartVersion + 2);
#endif
} /* end ProcessPeerMsg_UnSubMsg_Nominal() */

static void ProcessPeerMsg_NoMsg_Nominal(void)
{
    START();

    UT_ResetState(0);
    SBN_BufferDesc_t BufD;

    memset(&BufD, 0, sizeof(BufD));

    BufD.MsgType = SBN_NO_MSG;

    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_SUCCESS);
} /* end ProcessPeerMsg_NoMsg_Nominal() */

static void ProcessPeerMsg_MsgErr(void)
{
    START();

    UT_ResetState(0);
    SBN_BufferDesc_t BufD;

    memset(&BufD, 0, sizeof(BufD));

    /* send a net message of an invalid type */
    BufD.MsgType = SBN_NO_MSG + 100;

    UtAssert_INT32_EQ(SBN_Inbound_ProcessMsg(&BufD, PeerRec), SBN_ERROR);
} /* end ProcessPeerMsg_MsgErr() */

static void Test_SBN_Inbound_ProcessMsg(void)
{
    ProcessPeerMsg_AppMsg_FiltErr();
    ProcessPeerMsg_AppMsg_FiltOut();
    ProcessPeerMsg_AppMsg_PassMsgErr();
    ProcessPeerMsg_MsgErr();

    ProcessPeerMsg_AppMsg_Nominal();
    ProcessPeerMsg_SubMsg_Nominal();
    ProcessPeerMsg_UnSubMsg_Nominal();
    ProcessPeerMsg_NoMsg_Nominal();
} /* end Test_SBN_Inbound_ProcessMsg() */


void UT_Setup(void)
{
} /* end UT_Setup() */

void UT_TearDown(void)
{
} /* end UT_TearDown() */

void UtTest_Setup(void)
{
    ADD_TEST(SBN_Inbound_ProcessMsg);
}
