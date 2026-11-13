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
#include "sbn_worker_task.h"
#include "sbn_outbound_task.h"

SBN_Status_t SBN_Filter_ProcessModifiers(SBN_PeerRecord_t *PeerRec, CFE_MSG_Message_t *MsgPtr)
{
    SBN_Status_t SBN_Status;
    SBN_ModuleIdx_t      FilterIdx;
    SBN_Filter_Ctx_t     Filter_Context;
    const SBN_FilterInterface_t *FilterIntf;

    SBN_Status = SBN_SUCCESS;

    Filter_Context.MyProcessorID  = CFE_PSP_GetProcessorId();
    Filter_Context.MySpacecraftID = CFE_PSP_GetSpacecraftId();
    Filter_Context.PeerProcessorID  = PeerRec->Intf.ProcessorID;
    Filter_Context.PeerSpacecraftID = PeerRec->Intf.SpacecraftID;

    for (FilterIdx = 0; SBN_Status == SBN_SUCCESS && FilterIdx < PeerRec->Intf.FilterCnt; FilterIdx++)
    {
        FilterIntf = PeerRec->Intf.Filters[FilterIdx];

        if (FilterIntf == NULL || FilterIntf->FilterSend == NULL)
        {
            continue;
        }

        SBN_Status = (FilterIntf->FilterSend)(MsgPtr, &Filter_Context);
    }

    return SBN_Status;
}
