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

#ifndef SBN_OUTBOUND_TASK_H
#define SBN_OUTBOUND_TASK_H

/*
 ** Include Files
 */
#include "sbn_interfaces.h"
#include "sbn_resource.h"

/**
 * Sends a message to a peer using the module's send API.
 *
 * @param MsgType SBN type of the message
 * @param MsgSz Size of the message
 * @param Msg Message to send
 * @param Peer The peer to send the message to.
 * @return Number of characters sent on success, -1 on error.
 *
 */
SBN_Status_t SBN_Outbound_SendToPeer(SBN_PeerInterface_t *Peer, SBN_BufferDesc_t *D);

void SBN_OutboundPeerDataJob(SBN_PeerRecord_t *PeerRec, SBN_BufferDesc_t *D);

void SBN_OutboundPollCycle(void);

#endif
