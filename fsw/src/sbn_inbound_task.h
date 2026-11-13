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

#ifndef SBN_INBOUND_TASK_H
#define SBN_INBOUND_TASK_H

/*
 ** Include Files
 */
#include <string.h>

#include "sbn_error.h"
#include "sbn_app.h"
#include "sbn_worker_task.h"
#include "sbn_resource.h"
//jphfix #include "cfe_es.h" /* PerfLog */
//jphfix #include "cfe_platform_cfg.h"

//jphfix rename to inbound
//jphfix void SBN_RecvPeerJob(SBN_RecvPeerTaskData_t *D);
//jphfix void SBN_RecvNetJob(SBN_RecvNetTaskData_t *D);
void SBN_InboundPeerDataJob(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D);
void SBN_InboundNetDataJob(SBN_NetRecord_t *NetRec, SBN_BufferDesc_t *D);
void SBN_InboundPollCycle(void);

#endif
