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

#define SBN_NUM_WORKER_TASKS    4

/* Keep this to a power of 2, >= the number of worker tasks */
#define SBN_WORKER_QUEUE_DEPTH  4

/* A small amount of extra buffer space for the SBN protocol messagse */
#define SBN_WORKER_BUFFER_SZ   (SBN_PACKED_HDR_SZ + CFE_MISSION_SB_MAX_SB_MSG_SIZE)

#define SBN_WORKER_TASK_STACK_SIZE 16384
#define SBN_WORKER_TASK_PRIORITY   85

#define SBN_WORKER_WAIT_TIME_MSEC  100

typedef enum
{
    SBN_WorkerTask_JobType_NONE,
    SBN_WorkerTask_JobType_PEER_OUTBOUND,
    SBN_WorkerTask_JobType_PEER_INBOUND,
    SBN_WorkerTask_JobType_NET_INBOUND
} SBN_WorkerTask_JobType_t;

typedef struct SBN_WorkerTask_WorkItem
{
    SBN_WorkerTask_JobType_t JobType;

    SBN_NetIdx_t  NetIdx;
    SBN_PeerIdx_t PeerIdx;

} SBN_WorkerTask_WorkItem_t;

typedef struct SBN_WorkerTask_Context
{
    CFE_ES_TaskId_t TaskId;

    SBN_WorkerTask_WorkItem_t Assignment;
    uint32 NumCompleted;

    uint8 MessageBuffer[SBN_WORKER_BUFFER_SZ];
} SBN_WorkerTask_Context_t;

typedef struct SBN_WorkerTask_WorkQueue
{
    SBN_WorkerTask_WorkItem_t WorkItems[SBN_WORKER_QUEUE_DEPTH];

    SBN_WorkerTask_Context_t Tasks[SBN_NUM_WORKER_TASKS];

    /* main "gate" to hold all workers, if needed */
    bool   ShouldRun;

    /* instructs all workers to exit themselves */
    bool   ShouldExit;

    uint32 NumPosted;
    uint32 NumClaimed;
    uint32 NumCompleted;
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
