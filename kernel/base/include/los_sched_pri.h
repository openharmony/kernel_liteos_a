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

#ifndef _LOS_SCHED_PRI_H
#define _LOS_SCHED_PRI_H

#include "los_sortlink_pri.h"
#include "los_sys_pri.h"
#include "los_hwi.h"
#include "hal_timer.h"
#ifdef LOSCFG_SCHED_DEBUG
#include "los_stat_pri.h"
#endif
#include "los_stackinfo_pri.h"
#include "los_futex_pri.h"
#include "los_signal.h"
#ifdef LOSCFG_KERNEL_CPUP
#include "los_cpup_pri.h"
#endif
#ifdef LOSCFG_KERNEL_LITEIPC
#include "hm_liteipc.h"
#endif

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#define OS_SCHED_MINI_PERIOD       (OS_SYS_CLOCK / LOSCFG_BASE_CORE_TICK_PER_SECOND_MINI)
#define OS_TICK_RESPONSE_PRECISION (UINT32)((OS_SCHED_MINI_PERIOD * 75) / 100)
#define OS_SCHED_MAX_RESPONSE_TIME (UINT64)(((UINT64)-1) - 1U)

extern UINT32 g_taskScheduled;
#define OS_SCHEDULER_ACTIVE (g_taskScheduled & (1U << ArchCurrCpuid()))
#define OS_SCHEDULER_ALL_ACTIVE (g_taskScheduled == LOSCFG_KERNEL_CPU_MASK)

typedef BOOL (*SCHED_TL_FIND_FUNC)(UINTPTR, UINTPTR);

STATIC INLINE UINT64 OsGetCurrSchedTimeCycle(VOID)
{
    return HalClockGetCycles();
}

typedef enum {
    INT_NO_RESCH = 0x0,   /* no needs to schedule */
    INT_PEND_RESCH = 0x1, /* pending schedule flag */
    INT_PEND_TICK = 0x2,  /* pending tick */
} SchedFlag;

typedef struct {
    SortLinkAttribute taskSortLink;          /* task sort link */
    SortLinkAttribute swtmrSortLink;         /* swtmr sort link */
    UINT64            responseTime;          /* Response time for current CPU tick interrupts */
    UINT32            responseID;            /* The response ID of the current CPU tick interrupt */
    UINT32            idleTaskID;            /* idle task id */
    UINT32            taskLockCnt;           /* task lock flag */
    UINT32            swtmrTaskID;           /* software timer task id */
    UINT32            swtmrHandlerQueue;     /* software timer timeout queue id */
    UINT32            schedFlag;             /* pending scheduler flag */
} SchedRunQue;

extern SchedRunQue g_schedRunQue[LOSCFG_KERNEL_CORE_NUM];

STATIC INLINE SchedRunQue *OsSchedRunQue(VOID)
{
    return &g_schedRunQue[ArchCurrCpuid()];
}

STATIC INLINE SchedRunQue *OsSchedRunQueByID(UINT16 id)
{
    return &g_schedRunQue[id];
}

STATIC INLINE UINT32 OsSchedLockCountGet(VOID)
{
    return OsSchedRunQue()->taskLockCnt;
}

STATIC INLINE VOID OsSchedLockSet(UINT32 count)
{
    OsSchedRunQue()->taskLockCnt = count;
}

STATIC INLINE VOID OsSchedLock(VOID)
{
    OsSchedRunQue()->taskLockCnt++;
}

STATIC INLINE VOID OsSchedUnlock(VOID)
{
    OsSchedRunQue()->taskLockCnt--;
}

STATIC INLINE BOOL OsSchedUnlockResch(VOID)
{
    SchedRunQue *rq = OsSchedRunQue();
    if (rq->taskLockCnt > 0) {
        rq->taskLockCnt--;
        if ((rq->taskLockCnt == 0) && (rq->schedFlag & INT_PEND_RESCH) && OS_SCHEDULER_ACTIVE) {
            return TRUE;
        }
    }

    return FALSE;
}

STATIC INLINE BOOL OsSchedIsLock(VOID)
{
    return (OsSchedRunQue()->taskLockCnt != 0);
}

/* Check if preemptible with counter flag */
STATIC INLINE BOOL OsPreemptable(VOID)
{
    SchedRunQue *rq = OsSchedRunQue();
    /*
     * Unlike OsPreemptableInSched, the int may be not disabled when OsPreemptable
     * is called, needs manually disable interrupt, to prevent current task from
     * being migrated to another core, and get the wrong preemptable status.
     */
    UINT32 intSave = LOS_IntLock();
    BOOL preemptible = (rq->taskLockCnt == 0);
    if (!preemptible) {
        /* Set schedule flag if preemption is disabled */
        rq->schedFlag |= INT_PEND_RESCH;
    }
    LOS_IntRestore(intSave);
    return preemptible;
}

STATIC INLINE BOOL OsPreemptableInSched(VOID)
{
    BOOL preemptible = FALSE;
    SchedRunQue *rq = OsSchedRunQue();

#ifdef LOSCFG_KERNEL_SMP
    /*
     * For smp systems, schedule must hold the task spinlock, and this counter
     * will increase by 1 in that case.
     */
    preemptible = (rq->taskLockCnt == 1);

#else
    preemptible = (rq->taskLockCnt == 0);
#endif
    if (!preemptible) {
        /* Set schedule flag if preemption is disabled */
        rq->schedFlag |= INT_PEND_RESCH;
    }

    return preemptible;
}

STATIC INLINE UINT32 OsSchedGetRunQueIdle(VOID)
{
    return OsSchedRunQue()->idleTaskID;
}

STATIC INLINE VOID OsSchedRunQuePendingSet(VOID)
{
    OsSchedRunQue()->schedFlag |= INT_PEND_RESCH;
}

#ifdef LOSCFG_KERNEL_SMP
STATIC INLINE VOID FindIdleRunQue(UINT16 *idleCpuID)
{
    SchedRunQue *idleRq = OsSchedRunQueByID(0);
    UINT32 nodeNum = OsGetSortLinkNodeNum(&idleRq->taskSortLink) + OsGetSortLinkNodeNum(&idleRq->swtmrSortLink);
    UINT16 cpuID = 1;
    do {
        SchedRunQue *rq = OsSchedRunQueByID(cpuID);
        UINT32 temp = OsGetSortLinkNodeNum(&rq->taskSortLink) + OsGetSortLinkNodeNum(&rq->swtmrSortLink);
        if (nodeNum > temp) {
            *idleCpuID = cpuID;
            nodeNum = temp;
        }
        cpuID++;
    } while (cpuID < LOSCFG_KERNEL_CORE_NUM);
}
#endif

STATIC INLINE VOID OsSchedAddTask2TimeList(SortLinkList *node, UINT64 startTime, UINT32 waitTicks)
{
    UINT16 idleCpu = 0;
#ifdef LOSCFG_KERNEL_SMP
    FindIdleRunQue(&idleCpu);
#endif
    SchedRunQue *rq = OsSchedRunQueByID(idleCpu);
    UINT64 responseTime = startTime + (UINT64)waitTicks * OS_CYCLE_PER_TICK;
    OsAdd2SortLink(&rq->taskSortLink, node, responseTime, idleCpu);
}

STATIC INLINE UINT32 OsSchedSwtmrHandlerQueueGet(VOID)
{
    return OsSchedRunQue()->swtmrHandlerQueue;
}

STATIC INLINE VOID OsSchedDeTaskFromTimeList(SortLinkList *node)
{
#ifdef LOSCFG_KERNEL_SMP
    SchedRunQue *rq = OsSchedRunQueByID(node->cpuid);
#else
    SchedRunQue *rq = OsSchedRunQueByID(0);
#endif
    OsDeleteFromSortLink(&rq->taskSortLink, node);
}

STATIC INLINE VOID OsSchedAddSwtmr2TimeList(SortLinkList *node, UINT64 startTime, UINT32 waitTicks)
{
    UINT16 idleCpu = 0;
#ifdef LOSCFG_KERNEL_SMP
    FindIdleRunQue(&idleCpu);
#endif
    SchedRunQue *rq = OsSchedRunQueByID(idleCpu);
    UINT64 responseTime = startTime + (UINT64)waitTicks * OS_CYCLE_PER_TICK;
    OsAdd2SortLink(&rq->swtmrSortLink, node, responseTime, idleCpu);
}

STATIC INLINE VOID OsSchedDeSwtmrFromTimeList(SortLinkList *node)
{
#ifdef LOSCFG_KERNEL_SMP
    SchedRunQue *rq = OsSchedRunQueByID(node->cpuid);
#else
    SchedRunQue *rq = OsSchedRunQueByID(0);
#endif
    OsDeleteFromSortLink(&rq->swtmrSortLink, node);
}

VOID OsSchedRunQueIdleInit(UINT32 idleTaskID);
VOID OsSchedRunQueSwtmrInit(UINT32 swtmrTaskID, UINT32 swtmrQueue);
VOID OsSchedRunQueInit(VOID);
BOOL OsSchedSwtmrTimeListFind(SCHED_TL_FIND_FUNC checkFunc, UINTPTR arg);

/**
 * @ingroup los_sched
 * Define a usable task priority.
 *
 * Highest task priority.
 */
#define OS_TASK_PRIORITY_HIGHEST    0

/**
 * @ingroup los_sched
 * Define a usable task priority.
 *
 * Lowest task priority.
 */
#define OS_TASK_PRIORITY_LOWEST     31

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is init.
 */
#define OS_TASK_STATUS_INIT         0x0001U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is ready.
 */
#define OS_TASK_STATUS_READY        0x0002U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is running.
 */
#define OS_TASK_STATUS_RUNNING      0x0004U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is suspended.
 */
#define OS_TASK_STATUS_SUSPENDED    0x0008U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is blocked.
 */
#define OS_TASK_STATUS_PENDING      0x0010U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is delayed.
 */
#define OS_TASK_STATUS_DELAY        0x0020U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The time for waiting for an event to occur expires.
 */
#define OS_TASK_STATUS_TIMEOUT      0x0040U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is pend for a period of time.
 */
#define OS_TASK_STATUS_PEND_TIME    0x0080U

/**
 * @ingroup los_sched
 * Flag that indicates the task or task control block status.
 *
 * The task is exit.
 */
#define OS_TASK_STATUS_EXIT         0x0100U

#define OS_TCB_NAME_LEN             32

typedef struct {
    VOID            *stackPointer;      /**< Task stack pointer */
    UINT16          taskStatus;         /**< Task status */

    /* The scheduling */
    UINT16          basePrio;
    UINT16          priority;           /**< Task priority */
    UINT16          policy;
    UINT64          startTime;          /**< The start time of each phase of task */
    UINT64          irqStartTime;       /**< Interrupt start time */
    UINT32          irqUsedTime;        /**< Interrupt consumption time */
    UINT32          initTimeSlice;      /**< Task init time slice */
    INT32           timeSlice;          /**< Task remaining time slice */
    UINT32          waitTimes;          /**< Task delay time, tick number */
    SortLinkList    sortList;           /**< Task sortlink node */

    UINT32          stackSize;          /**< Task stack size */
    UINTPTR         topOfStack;         /**< Task stack top */
    UINT32          taskID;             /**< Task ID */
    TSK_ENTRY_FUNC  taskEntry;          /**< Task entrance function */
    VOID            *joinRetval;        /**< pthread adaption */
    VOID            *taskMux;           /**< Task-held mutex */
    VOID            *taskEvent;         /**< Task-held event */
    UINTPTR         args[4];            /**< Parameter, of which the maximum number is 4 */
    CHAR            taskName[OS_TCB_NAME_LEN]; /**< Task name */
    LOS_DL_LIST     pendList;           /**< Task pend node */
    LOS_DL_LIST     threadList;         /**< thread list */
    UINT32          eventMask;          /**< Event mask */
    UINT32          eventMode;          /**< Event mode */
    UINT32          priBitMap;          /**< BitMap for recording the change of task priority,
                                             the priority can not be greater than 31 */
#ifdef LOSCFG_KERNEL_CPUP
    OsCpupBase      taskCpup;           /**< task cpu usage */
#endif
    INT32           errorNo;            /**< Error Num */
    UINT32          signal;             /**< Task signal */
    sig_cb          sig;
#ifdef LOSCFG_KERNEL_SMP
    UINT16          currCpu;            /**< CPU core number of this task is running on */
    UINT16          lastCpu;            /**< CPU core number of this task is running on last time */
    UINT16          cpuAffiMask;        /**< CPU affinity mask, support up to 16 cores */
#ifdef LOSCFG_KERNEL_SMP_TASK_SYNC
    UINT32          syncSignal;         /**< Synchronization for signal handling */
#endif
#ifdef LOSCFG_KERNEL_SMP_LOCKDEP
    LockDep         lockDep;
#endif
#endif
#ifdef LOSCFG_SCHED_DEBUG
    SchedStat       schedStat;          /**< Schedule statistics */
#endif
#ifdef LOSCFG_KERNEL_VM
    UINTPTR         archMmu;
    UINTPTR         userArea;
    UINTPTR         userMapBase;
    UINT32          userMapSize;        /**< user thread stack size ,real size : userMapSize + USER_STACK_MIN_SIZE */
    FutexNode       futex;
#endif
    UINT32          processID;          /**< Which belong process */
    LOS_DL_LIST     joinList;           /**< join list */
    LOS_DL_LIST     lockList;           /**< Hold the lock list */
    UINTPTR         waitID;             /**< Wait for the PID or GID of the child process */
    UINT16          waitFlag;           /**< The type of child process that is waiting, belonging to a group or parent,
                                             a specific child process, or any child process */
#ifdef LOSCFG_KERNEL_LITEIPC
    IpcTaskInfo     *ipcTaskInfo;
#endif
#ifdef LOSCFG_KERNEL_PERF
    UINTPTR         pc;
    UINTPTR         fp;
#endif
} LosTaskCB;

STATIC INLINE BOOL OsTaskIsRunning(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & OS_TASK_STATUS_RUNNING) != 0);
}

STATIC INLINE BOOL OsTaskIsReady(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & OS_TASK_STATUS_READY) != 0);
}

STATIC INLINE BOOL OsTaskIsInactive(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & (OS_TASK_STATUS_INIT | OS_TASK_STATUS_EXIT)) != 0);
}

STATIC INLINE BOOL OsTaskIsPending(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & OS_TASK_STATUS_PENDING) != 0);
}

STATIC INLINE BOOL OsTaskIsSuspended(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & OS_TASK_STATUS_SUSPENDED) != 0);
}

STATIC INLINE BOOL OsTaskIsBlocked(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & (OS_TASK_STATUS_SUSPENDED | OS_TASK_STATUS_PENDING | OS_TASK_STATUS_DELAY)) != 0);
}

STATIC INLINE LosTaskCB *OsCurrTaskGet(VOID)
{
    return (LosTaskCB *)ArchCurrTaskGet();
}

STATIC INLINE VOID OsCurrTaskSet(LosTaskCB *task)
{
    ArchCurrTaskSet(task);
}

STATIC INLINE VOID OsCurrUserTaskSet(UINTPTR thread)
{
    ArchCurrUserTaskSet(thread);
}

STATIC INLINE VOID OsSchedIrqUpdateUsedTime(VOID)
{
    LosTaskCB *runTask = OsCurrTaskGet();
    runTask->irqUsedTime = OsGetCurrSchedTimeCycle() - runTask->irqStartTime;
}

STATIC INLINE VOID OsSchedIrqStartTime(VOID)
{
    LosTaskCB *runTask = OsCurrTaskGet();
    runTask->irqStartTime = OsGetCurrSchedTimeCycle();
}

/*
 * Schedule flag, one bit represents one core.
 * This flag is used to prevent kernel scheduling before OSStartToRun.
 */
#define OS_SCHEDULER_SET(cpuid) do {     \
    g_taskScheduled |= (1U << (cpuid));  \
} while (0);

#define OS_SCHEDULER_CLR(cpuid) do {     \
    g_taskScheduled &= ~(1U << (cpuid)); \
} while (0);

VOID OsSchedSetIdleTaskSchedParam(LosTaskCB *idleTask);
VOID OsSchedResetSchedResponseTime(UINT64 responseTime);
VOID OsSchedUpdateExpireTime(VOID);
VOID OsSchedToUserReleaseLock(VOID);
VOID OsSchedTaskDeQueue(LosTaskCB *taskCB);
VOID OsSchedTaskEnQueue(LosTaskCB *taskCB);
UINT32 OsSchedTaskWait(LOS_DL_LIST *list, UINT32 timeout, BOOL needSched);
VOID OsSchedTaskWake(LosTaskCB *resumedTask);
BOOL OsSchedModifyTaskSchedParam(LosTaskCB *taskCB, UINT16 policy, UINT16 priority);
BOOL OsSchedModifyProcessSchedParam(UINT32 pid, UINT16 policy, UINT16 priority);
VOID OsSchedSuspend(LosTaskCB *taskCB);
BOOL OsSchedResume(LosTaskCB *taskCB);
VOID OsSchedDelay(LosTaskCB *runTask, UINT32 tick);
VOID OsSchedYield(VOID);
VOID OsSchedTaskExit(LosTaskCB *taskCB);
VOID OsSchedTick(VOID);
UINT32 OsSchedInit(VOID);
VOID OsSchedStart(VOID);

/*
 * This function simply picks the next task and switches to it.
 * Current task needs to already be in the right state or the right
 * queues it needs to be in.
 */
VOID OsSchedResched(VOID);
VOID OsSchedIrqEndCheckNeedSched(VOID);

/*
* This function inserts the runTask to the lock pending list based on the
* task priority.
*/
LOS_DL_LIST *OsSchedLockPendFindPos(const LosTaskCB *runTask, LOS_DL_LIST *lockList);

#ifdef LOSCFG_SCHED_TICK_DEBUG
VOID OsSchedDebugRecordData(VOID);
#endif

UINT32 OsShellShowTickRespo(VOID);

UINT32 OsShellShowSchedParam(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_SCHED_PRI_H */
