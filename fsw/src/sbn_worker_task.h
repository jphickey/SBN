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

#ifndef SBN_WORKER_TASK_H
#define SBN_WORKER_TASK_H

#include "common_types.h"
#include "cfe_es_api_typedefs.h"
#include "cfe_mission_cfg.h"
#include "sbn_types.h"
#include "sbn_interfaces.h"
//jphfix #include "sbn_resource.h"

#define SBN_NUM_WORKER_TASKS    4

/* Keep this to a power of 2, >= the number of worker tasks + max subscriptions */
#define SBN_WORKER_QUEUE_DEPTH  512

/* A small amount of extra buffer space for the SBN protocol messagse */
#define SBN_WORKER_BUFFER_SZ   (SBN_PACKED_HDR_SZ + CFE_MISSION_SB_MAX_SB_MSG_SIZE)

/* jphfix - this should be smaller */
#define SBN_WORKER_TASK_STACK_SIZE 262144
#define SBN_WORKER_TASK_PRIORITY   85

#define SBN_WORKER_WAIT_TIME_MSEC  100

typedef enum
{
    SBN_WorkerTask_JobType_NONE,
    SBN_WorkerTask_JobType_CHECK_CONNECTION,
    SBN_WorkerTask_JobType_CONNECTION_CHANGE,
    SBN_WorkerTask_JobType_PEER_OUTBOUND,
    SBN_WorkerTask_JobType_PEER_INBOUND,
    SBN_WorkerTask_JobType_NET_INBOUND,
    SBN_WorkerTask_JobType_ADD_SUBSCRIPTION,
    SBN_WorkerTask_JobType_REMOVE_SUBSCRIPTION,
} SBN_WorkerTask_JobType_t;

typedef struct SBN_WorkerTask_CommsInfo
{
    SBN_NetId_t  NetId;
    SBN_PeerId_t PeerId;

} SBN_WorkerTask_CommsInfo_t;

typedef struct SBN_WorkerTask_SubsInfo
{
    CFE_SB_MsgId_t MsgId;
    CFE_SB_Qos_t   Qos;

} SBN_WorkerTask_SubsInfo_t;

typedef union SBN_WorkerTask_Info
{
    SBN_WorkerTask_CommsInfo_t Comms;
    SBN_WorkerTask_SubsInfo_t  Subs;
} SBN_WorkerTask_Info_t;


typedef struct SBN_WorkerTask_WorkItem
{
    SBN_WorkerTask_JobType_t JobType;

    SBN_WorkerTask_Info_t Info;

} SBN_WorkerTask_WorkItem_t;

typedef struct SBN_WorkerTask_Context
{
    CFE_ES_TaskId_t TaskId;

    SBN_WorkerTask_WorkItem_t Assignment;
    uint32 NumCompleted;

    SBN_BufferDesc_t BufferDesc;

    union
    {
        uint8 MessageBuffer[SBN_WORKER_BUFFER_SZ];
        double AlignDbl;
        unsigned long long AlignLong;
    } u;

} SBN_WorkerTask_Context_t;

typedef struct SBN_WorkerTask_WorkQueue
{
    SBN_WorkerTask_WorkItem_t WorkItems[SBN_WORKER_QUEUE_DEPTH];

    SBN_WorkerTask_Context_t Tasks[SBN_NUM_WORKER_TASKS];

    /* gate to hold all workers, if needed */
    volatile bool   ShouldRun;

    /* instructs all workers to exit themselves */
    volatile bool   ShouldExit;

    volatile uint32 NumPosted;
    volatile uint32 NumClaimed;
    volatile uint32 NumCompleted;

    uint32 NumWorkers;

    osal_id_t Sync;
} SBN_WorkerTask_WorkQueue_t;

SBN_Status_t SBN_WorkerTask_Setup(void);
void SBN_WorkerTask_Shutdown(void);

bool SBN_WorkerTask_IsIdle(void);
void SBN_WorkerTask_Pause(void);
void SBN_WorkerTask_Resume(void);
bool SBN_WorkerTask_ScheduleJob(const SBN_WorkerTask_WorkItem_t *Job);


#endif /* _sbn_pack_h_ */
