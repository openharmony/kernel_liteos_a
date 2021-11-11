/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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

#ifndef _LOS_PERCPU_PRI_H
#define _LOS_PERCPU_PRI_H

#include "los_base.h"
#include "los_hw_cpu.h"
#include "los_spinlock.h"
#include "los_sortlink_pri.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#ifdef LOSCFG_KERNEL_SMP
typedef enum {
    CPU_RUNNING = 0,   /* cpu is running */
    CPU_HALT,          /* cpu in the halt */
    CPU_EXC            /* cpu in the exc */
} ExcFlag;
#endif

typedef struct {
    SortLinkAttribute taskSortLink;          /* task sort link */
    SPIN_LOCK_S       taskSortLinkSpin;      /* task sort link spin lock */
    SortLinkAttribute swtmrSortLink;         /* swtmr sort link */
    SPIN_LOCK_S       swtmrSortLinkSpin;     /* swtmr sort link spin lock */
    UINT64            responseTime;          /* Response time for current CPU tick interrupts */
    UINT64            tickStartTime;         /* The time when the tick interrupt starts processing */
    UINT32            responseID;            /* The response ID of the current CPU tick interrupt */
    UINTPTR           runProcess;            /* The address of the process control block pointer to which
                                                the current CPU is running */
    UINT32            idleTaskID;            /* idle task id */
    UINT32            taskLockCnt;           /* task lock flag */
    UINT32            swtmrHandlerQueue;     /* software timer timeout queue id */
    UINT32            swtmrTaskID;           /* software timer task id */

    UINT32            schedFlag;             /* pending scheduler flag */
#ifdef LOSCFG_KERNEL_SMP
    UINT32            excFlag;               /* cpu halt or exc flag */
#ifdef LOSCFG_KERNEL_SMP_CALL
    LOS_DL_LIST       funcLink;              /* mp function call link */
#endif
#endif
} Percpu;

/* the kernel per-cpu structure */
extern Percpu g_percpu[LOSCFG_KERNEL_CORE_NUM];

STATIC INLINE Percpu *OsPercpuGet(VOID)
{
    return &g_percpu[ArchCurrCpuid()];
}

STATIC INLINE Percpu *OsPercpuGetByID(UINT32 cpuid)
{
    return &g_percpu[cpuid];
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_PERCPU_PRI_H */
