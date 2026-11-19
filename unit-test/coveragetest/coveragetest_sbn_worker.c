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
#include "sbn_worker.h"
#include "sbn_error.h"

void Test_SBN_Worker_GetAssignment(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_GetAssignment(SBN_Worker_WorkItem_t *Assignment);
     */
    SBN_Worker_WorkItem_t Assignment;

    memset(&Assignment, 0xFF, sizeof(Assignment));

    SBN_WorkQueue.NumPosted = 1;

    UtAssert_VOIDCALL(SBN_Worker_GetAssignment(&Assignment));
}

static void UT_SBN_Worker_NetJobFunc(SBN_NetRecord_t *NetRec, SBN_BufferDesc_t *BufD)
{
    UT_DEFAULT_IMPL(UT_SBN_Worker_NetJobFunc);
}

void Test_SBN_Worker_DispatchToNet(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_DispatchToNet(SBN_Worker_TaskCtxt_t *Ctxt, SBN_NetId_t NetId, SBN_Worker_NetJobFunc_t HandlerFunc);
     */
    SBN_Worker_TaskCtxt_t Ctxt;
    SBN_NetId_t NetId = SBN_AppData.NetRecords[0].NetId;

    UT_SetDefaultReturnValue(UT_KEY(SBN_LocateNetRecordByID), (UT_IntReturn_t) &SBN_AppData.NetRecords[0]);

    memset(&Ctxt, 0, sizeof(Ctxt));
    UtAssert_VOIDCALL(SBN_Worker_DispatchToNet(&Ctxt, NetId, UT_SBN_Worker_NetJobFunc));
    UtAssert_STUB_COUNT(UT_SBN_Worker_NetJobFunc, 1);
}

static void UT_SBN_Worker_PeerJobFunc(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *BufD)
{
    UT_DEFAULT_IMPL(UT_SBN_Worker_PeerJobFunc);
}

void Test_SBN_Worker_DispatchToPeer(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_DispatchToPeer(SBN_Worker_TaskCtxt_t *Ctxt, SBN_PeerId_t PeerId, SBN_Worker_PeerJobFunc_t HandlerFunc);
     */
    SBN_Worker_TaskCtxt_t Ctxt;
    SBN_PeerId_t PeerId = SBN_AppData.PeerRecords[0].PeerId;

    memset(&Ctxt, 0, sizeof(Ctxt));

    UT_SetDefaultReturnValue(UT_KEY(SBN_LocatePeerRecordByID), (UT_IntReturn_t) &SBN_AppData.PeerRecords[0]);

    UtAssert_VOIDCALL(SBN_Worker_DispatchToPeer(&Ctxt, PeerId, UT_SBN_Worker_PeerJobFunc));
    UtAssert_STUB_COUNT(UT_SBN_Worker_PeerJobFunc, 1);
}

void Test_SBN_Worker_DispatchAssignment(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_DispatchAssignment(SBN_Worker_TaskCtxt_t *Ctxt, const SBN_Worker_WorkItem_t *Assignment);
     */
    SBN_Worker_TaskCtxt_t Ctxt;
    SBN_Worker_WorkItem_t Assignment;

    memset(&Ctxt, 0, sizeof(Ctxt));
    memset(&Assignment, 0, sizeof(Assignment));

    UtAssert_VOIDCALL(SBN_Worker_DispatchAssignment(&Ctxt, &Assignment));
}

void Test_SBN_Worker_EntryPoint(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_EntryPoint(void);
     */

    SBN_WorkQueue.ShouldExit = true;

    UtAssert_VOIDCALL(SBN_Worker_EntryPoint());
}

void Test_SBN_Worker_Setup(void)
{
    START();

    /* Test Case For:
     *  SBN_Status_t SBN_Worker_Setup(void);
     */
    UtAssert_VOIDCALL(SBN_Worker_Setup());
}

void Test_SBN_Worker_Shutdown(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_Shutdown(void);
     */
    UtAssert_VOIDCALL(SBN_Worker_Shutdown());
}

void Test_SBN_Worker_Pause(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_Pause(void);
     */
    UtAssert_VOIDCALL(SBN_Worker_Pause());
}

void Test_SBN_Worker_Resume(void)
{
    START();

    /* Test Case For:
     *  void SBN_Worker_Resume(void);
     */
    UtAssert_VOIDCALL(SBN_Worker_Resume());
}

void Test_SBN_Worker_ScheduleJob(void)
{
    START();

    /* Test Case For:
     *  bool SBN_Worker_ScheduleJob(uint32 *JobIdOut, const SBN_Worker_WorkItem_t *Job);
     */
    uint32 JobId;
    SBN_Worker_WorkItem_t Assignment;

    memset(&Assignment, 0, sizeof(Assignment));

    UtAssert_VOIDCALL(SBN_Worker_ScheduleJob(&JobId, &Assignment));
}

void Test_SBN_Worker_IsJobPending(void)
{
    START();

    /* Test Case For:
     *  bool SBN_Worker_IsJobPending(uint32 JobId);
     */
    uint32 JobId;

    JobId = -1;

    UtAssert_BOOL_FALSE(SBN_Worker_IsJobPending(JobId));
}



void UT_Setup(void)
{
    memset(&SBN_WorkQueue, 0, sizeof(SBN_WorkQueue));
} /* end UT_Setup() */

void UT_TearDown(void)
{
} /* end UT_TearDown() */

void UtTest_Setup(void)
{
    ADD_TEST(SBN_Worker_GetAssignment);
    ADD_TEST(SBN_Worker_DispatchToNet);
    ADD_TEST(SBN_Worker_DispatchToPeer);
    ADD_TEST(SBN_Worker_DispatchAssignment);
    ADD_TEST(SBN_Worker_EntryPoint);
    ADD_TEST(SBN_Worker_Setup);
    ADD_TEST(SBN_Worker_Shutdown);
    ADD_TEST(SBN_Worker_Pause);
    ADD_TEST(SBN_Worker_Resume);
    ADD_TEST(SBN_Worker_ScheduleJob);
    ADD_TEST(SBN_Worker_IsJobPending);
}
