/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LOS_CONTAINER_PRI_H
#define _LOS_CONTAINER_PRI_H

#include "los_atomic.h"
#ifdef LOSCFG_KERNEL_CONTAINER
#ifdef LOSCFG_PID_CONTAINER
#include "los_pid_container_pri.h"
#endif
#ifdef LOSCFG_UTS_CONTAINER
#include "los_uts_container_pri.h"
#endif

typedef enum {
    CONTAINER = 0,
    PID_CONTAINER,
    UTS_CONTAINER,
} ContainerType;

typedef struct Container {
    Atomic   rc;
#ifdef LOSCFG_PID_CONTAINER
    struct PidContainer *pidContainer;
#endif
#ifdef LOSCFG_UTS_CONTAINER
    struct UtsContainer *utsContainer;
#endif
} Container;

VOID OsContainerInitSystemProcess(LosProcessCB *processCB);

VOID OsInitRootContainer(VOID);

UINT32 OsCopyContainers(UINTPTR flags, LosProcessCB *child, LosProcessCB *parent, UINT32 *processID);

VOID OsContainersDestroy(LosProcessCB *processCB);

UINT32 OsAllocContainerID(VOID);

UINT32 OsGetContainerID(Container *container, ContainerType type);
#endif
#endif /* _LOS_CONTAINER_PRI_H */
