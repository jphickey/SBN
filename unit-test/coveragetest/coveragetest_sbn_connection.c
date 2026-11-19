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
#include "sbn_connection.h"
#include "sbn_error.h"

static void ProcessPeerMsg_ProtoMsg_VerErr(void)
{
    START();

    UT_ResetState(0);
    UT_CheckEvent_Setup(SBN_SB_EID, "SBN protocol version mismatch with peer ");

    uint8 ver = SBN_PROTO_VER + 1;

    SBN_BufferDesc_t BufD;
    memset(&BufD, 0, sizeof(BufD));

    BufD.MsgType      = SBN_PROTO_MSG;
    BufD.ActualDataSz = sizeof(ver);
    BufD.MaxDataSz    = BufD.ActualDataSz;
    BufD.DataPtr      = &ver;

    UtAssert_INT32_EQ(SBN_Connection_RecvProtocolVersionMsg(&BufD, PeerRec), SBN_ERROR);

    EVENT_CNT(1);
} /* end ProcessPeerMsg_ProtoMsg_VerErr() */

static void ProcessPeerMsg_ProtoMsg_Nominal(void)
{
    START();

    UT_ResetState(0);
    UT_CheckEvent_Setup(SBN_SB_EID, "SBN protocol version match with peer ");
    SBN_BufferDesc_t BufD;

    memset(&BufD, 0, sizeof(BufD));

    uint8 ver = SBN_PROTO_VER;

    BufD.MsgType      = SBN_PROTO_MSG;
    BufD.ActualDataSz = sizeof(ver);
    BufD.MaxDataSz    = BufD.ActualDataSz;
    BufD.DataPtr      = &ver;

    UtAssert_INT32_EQ(SBN_Connection_RecvProtocolVersionMsg(&BufD, PeerRec), SBN_SUCCESS);

    EVENT_CNT(1);
} /* end ProcessPeerMsg_ProtoMsg_Nominal() */

static void Test_SBN_ProcessPeerMsg(void)
{
    ProcessPeerMsg_ProtoMsg_VerErr();
    ProcessPeerMsg_ProtoMsg_Nominal();
} /* end Test_SBN_ProcessPeerMsg() */

static void Connected_AlreadyErr(void)
{
    START();

    UT_ResetState(0);
    UT_CheckEvent_Setup(SBN_PEER_EID, "ERROR: could not connect peer: ");

    /* Configuration Setup Rationale:
     * Peer Setup:
     * - PeerPtr->Connected: 1 - Pre-set peer as already connected to trigger the error condition */
    PeerPtr->Connected = 1;

    UtAssert_INT32_EQ(SBN_Connection_HandleConnect(PeerPtr), SBN_ERROR);
    UtAssert_INT32_EQ(PeerPtr->Connected, 1);
    EVENT_CNT(1);
} /* end Connected_AlreadyErr() */

static void Connected_Nominal(void)
{
    START();

    UT_ResetState(0);
    UtAssert_INT32_EQ(SBN_Connection_HandleConnect(PeerPtr), SBN_SUCCESS);

    UtAssert_INT32_EQ(PeerPtr->Connected, 1);
} /* end Connected_Nominal() */

static void Test_SBN_Connection_HandleConnect(void)
{
    Connected_AlreadyErr();
    Connected_Nominal();
} /* end Test_SBN_Connection_HandleConnect() */

static void Disconnected_ConnErr(void)
{
    START();

    UT_ResetState(0);
    UT_CheckEvent_Setup(SBN_PEER_EID, "ERROR: could not disconnect peer: already not connected to peer ");

    /* Configuration Setup Rationale:
     * Peer Setup:
     * - PeerPtr->Connected: 0 (default) - Peer starts disconnected to trigger the error condition
     * - PeerPtr->ProcessorID: ProcessorID - Set for error message identification */
    SBN_PeerInterface_t *PeerPtr = SBN_Peer_GetIntfData(&SBN_AppData.PeerRecords[0]);

    PeerPtr->ProcessorID = ProcessorID;

    UtAssert_INT32_EQ(SBN_Connection_HandleDisconnect(PeerPtr), SBN_ERROR);
    EVENT_CNT(1);
} /* end Disconnected_Nominal() */

static void Disconnected_Nominal(void)
{
    START();

    UT_ResetState(0);
    UT_CheckEvent_Setup(SBN_PEER_EID, "Disconnected from peer ");

    /* Configuration Setup Rationale:
     * Peer Setup:
     * - PeerPtr->ProcessorID: ProcessorID - Set for event message identification
     * - PeerPtr->Connected: 1 - Pre-set peer as connected to allow valid disconnection */
    SBN_PeerInterface_t *PeerPtr = SBN_Peer_GetIntfData(&SBN_AppData.PeerRecords[0]);

    PeerPtr->ProcessorID = ProcessorID;
    PeerPtr->Connected   = 1;

    UtAssert_INT32_EQ(SBN_Connection_HandleDisconnect(PeerPtr), SBN_SUCCESS);
    UtAssert_INT32_EQ(PeerPtr->Connected, 0);
    EVENT_CNT(1);
} /* end Disconnected_Nominal() */

static void Test_SBN_Connection_HandleDisconnect(void)
{
    Disconnected_ConnErr();
    Disconnected_Nominal();
} /* end Test_SBN_Connection_HandleDisconnect() */

void UT_Setup(void)
{
} /* end UT_Setup() */

void UT_TearDown(void)
{
} /* end UT_TearDown() */

void UtTest_Setup(void)
{
    ADD_TEST(SBN_ProcessPeerMsg);
    ADD_TEST(SBN_Connection_HandleConnect);
    ADD_TEST(SBN_Connection_HandleDisconnect);
}
