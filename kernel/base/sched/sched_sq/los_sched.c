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

#include "los_sched_pri.h"
#include "los_hw_pri.h"
#include "los_task_pri.h"
#include "los_swtmr_pri.h"
#include "los_process_pri.h"
#include "los_arch_mmu.h"
#include "los_hook.h"
#ifdef LOSCFG_KERNEL_CPUP
#include "los_cpup_pri.h"
#endif
#include "los_hw_tick_pri.h"
#include "los_tick_pri.h"
#ifdef LOSCFG_BASE_CORE_TSK_MONITOR
#include "los_stackinfo_pri.h"
#endif
#include "los_mp.h"
#ifdef LOSCFG_SCHED_DEBUG
#include "los_stat_pri.h"
#endif
#include "los_pm_pri.h"

#define OS_32BIT_MAX               0xFFFFFFFFUL
#define OS_SCHED_FIFO_TIMEOUT      0x7FFFFFFF
#define OS_PRIORITY_QUEUE_NUM      32
#define PRIQUEUE_PRIOR0_BIT        0x80000000U
#define OS_SCHED_TIME_SLICES_MIN   ((5000 * OS_SYS_NS_PER_US) / OS_NS_PER_CYCLE)  /* 5ms */
#define OS_SCHED_TIME_SLICES_MAX   ((LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT * OS_SYS_NS_PER_US) / OS_NS_PER_CYCLE)
#define OS_SCHED_TIME_SLICES_DIFF  (OS_SCHED_TIME_SLICES_MAX - OS_SCHED_TIME_SLICES_MIN)
#define OS_SCHED_READY_MAX         30
#define OS_TIME_SLICE_MIN          (INT32)((50 * OS_SYS_NS_PER_US) / OS_NS_PER_CYCLE) /* 50us */

#define OS_TASK_STATUS_BLOCKED     (OS_TASK_STATUS_INIT | OS_TASK_STATUS_PENDING | \
                                    OS_TASK_STATUS_DELAY | OS_TASK_STATUS_PEND_TIME)

typedef struct {
    LOS_DL_LIST priQueueList[OS_PRIORITY_QUEUE_NUM];
    UINT32      readyTasks[OS_PRIORITY_QUEUE_NUM];
    UINT32      queueBitmap;
} SchedQueue;

typedef struct {
    SchedQueue queueList[OS_PRIORITY_QUEUE_NUM];
    UINT32     queueBitmap;
} Sched;

SchedRunQue g_schedRunQue[LOSCFG_KERNEL_CORE_NUM];
STATIC Sched g_sched;

#ifdef LOSCFG_SCHED_TICK_DEBUG
#define OS_SCHED_DEBUG_DATA_NUM  1000
typedef struct {
    UINT32 tickResporeTime[OS_SCHED_DEBUG_DATA_NUM];
    UINT32 index;
    UINT32 setTickCount;
    UINT64 oldResporeTime;
} SchedTickDebug;
STATIC SchedTickDebug *g_schedTickDebug = NULL;

STATIC UINT32 OsSchedDebugInit(VOID)
{
    UINT32 size = sizeof(SchedTickDebug) * LOSCFG_KERNEL_CORE_NUM;
    g_schedTickDebug = (SchedTickDebug *)LOS_MemAlloc(m_aucSysMem0, size);
    if (g_schedTickDebug == NULL) {
        return LOS_ERRNO_TSK_NO_MEMORY;
    }

    (VOID)memset_s(g_schedTickDebug, size, 0, size);
    return LOS_OK;
}

VOID OsSchedDebugRecordData(VOID)
{
    SchedTickDebug *schedDebug = &g_schedTickDebug[ArchCurrCpuid()];
    if (schedDebug->index < OS_SCHED_DEBUG_DATA_NUM) {
        UINT64 currTime = OsGetCurrSchedTimeCycle();
        schedDebug->tickResporeTime[schedDebug->index] = currTime - schedDebug->oldResporeTime;
        schedDebug->oldResporeTime = currTime;
        schedDebug->index++;
    }
}

SchedTickDebug *OsSchedDebugGet(VOID)
{
    return g_schedTickDebug;
}

UINT32 OsShellShowTickRespo(VOID)
{
    UINT32 intSave;
    UINT16 cpu;
    UINT64 allTime;

    UINT32 tickSize = sizeof(SchedTickDebug) * LOSCFG_KERNEL_CORE_NUM;
    SchedTickDebug *schedDebug = (SchedTickDebug *)LOS_MemAlloc(m_aucSysMem1, tickSize);
    if (schedDebug == NULL) {
        return LOS_NOK;
    }

    UINT32 sortLinkNum[LOSCFG_KERNEL_CORE_NUM];
    SCHEDULER_LOCK(intSave);
    (VOID)memcpy_s((CHAR *)schedDebug, tickSize, (CHAR *)OsSchedDebugGet(), tickSize);
    (VOID)memset_s((CHAR *)OsSchedDebugGet(), tickSize, 0, tickSize);
    for (cpu = 0; cpu < LOSCFG_KERNEL_CORE_NUM; cpu++) {
        SchedRunQue *rq = OsSchedRunQueByID(cpu);
        sortLinkNum[cpu] = OsGetSortLinkNodeNum(&rq->taskSortLink) + OsGetSortLinkNodeNum(&rq->swtmrSortLink);
    }
    SCHEDULER_UNLOCK(intSave);

    for (cpu = 0; cpu < LOSCFG_KERNEL_CORE_NUM; cpu++) {
        SchedTickDebug *schedData = &schedDebug[cpu];
        PRINTK("cpu : %u sched data num : %u set time count : %u SortMax : %u\n",
               cpu, schedData->index, schedData->setTickCount, sortLinkNum[cpu]);
        UINT32 *data = schedData->tickResporeTime;
        allTime = 0;
        for (UINT32 i = 1; i < schedData->index; i++) {
            allTime += data[i];
            UINT32 timeUs = (data[i] * OS_NS_PER_CYCLE) / OS_SYS_NS_PER_US;
            PRINTK("     %u(%u)", timeUs, timeUs / OS_US_PER_TICK);
            if ((i != 0) && ((i % 5) == 0)) { /* A row of 5 data */
                PRINTK("\n");
            }
        }

        allTime = (allTime * OS_NS_PER_CYCLE) / OS_SYS_NS_PER_US;
        PRINTK("\nTick Indicates the average response period: %llu(us)\n", allTime / (schedData->index - 1));
    }

    (VOID)LOS_MemFree(m_aucSysMem1, schedDebug);
    return LOS_OK;
}

#else

UINT32 OsShellShowTickRespo(VOID)
{
    return LOS_NOK;
}
#endif

#ifdef LOSCFG_SCHED_DEBUG
UINT32 OsShellShowSchedParam(VOID)
{
    UINT64 averRunTime;
    UINT64 averTimeSlice;
    UINT64 averSchedWait;
    UINT64 averPendTime;
    UINT32 intSave;
    UINT32 size = g_taskMaxNum * sizeof(LosTaskCB);
    LosTaskCB *taskCBArray = LOS_MemAlloc(m_aucSysMem1, size);
    if (taskCBArray == NULL) {
        return LOS_NOK;
    }

    SCHEDULER_LOCK(intSave);
    (VOID)memcpy_s(taskCBArray, size, g_taskCBArray, size);
    SCHEDULER_UNLOCK(intSave);
    PRINTK("  Tid    AverRunTime(us)    SwitchCount  AverTimeSlice(us)    TimeSliceCount  AverReadyWait(us)  "
           "AverPendTime(us)  TaskName \n");
    for (UINT32 tid = 0; tid < g_taskMaxNum; tid++) {
        LosTaskCB *taskCB = taskCBArray + tid;
        if (OsTaskIsUnused(taskCB)) {
            continue;
        }

        averRunTime = 0;
        averTimeSlice = 0;
        averPendTime = 0;
        averSchedWait = 0;

        if (taskCB->schedStat.switchCount >= 1) {
            averRunTime = taskCB->schedStat.runTime / taskCB->schedStat.switchCount;
            averRunTime = (averRunTime * OS_NS_PER_CYCLE) / OS_SYS_NS_PER_US;
        }

        if (taskCB->schedStat.timeSliceCount > 1) {
            averTimeSlice = taskCB->schedStat.timeSliceTime / (taskCB->schedStat.timeSliceCount - 1);
            averTimeSlice = (averTimeSlice * OS_NS_PER_CYCLE) / OS_SYS_NS_PER_US;
        }

        if (taskCB->schedStat.pendCount > 1) {
            averPendTime = taskCB->schedStat.pendTime / taskCB->schedStat.pendCount;
            averPendTime = (averPendTime * OS_NS_PER_CYCLE) / OS_SYS_NS_PER_US;
        }

        if (taskCB->schedStat.waitSchedCount > 0) {
            averSchedWait = taskCB->schedStat.waitSchedTime / taskCB->schedStat.waitSchedCount;
            averSchedWait = (averSchedWait * OS_NS_PER_CYCLE) / OS_SYS_NS_PER_US;
        }

        PRINTK("%5u%19llu%15llu%19llu%18llu%19llu%18llu  %-32s\n", taskCB->taskID,
               averRunTime, taskCB->schedStat.switchCount,
               averTimeSlice, taskCB->schedStat.timeSliceCount - 1,
               averSchedWait, averPendTime, taskCB->taskName);
    }

    (VOID)LOS_MemFree(m_aucSysMem1, taskCBArray);

    return LOS_OK;
}

#else

UINT32 OsShellShowSchedParam(VOID)
{
    return LOS_NOK;
}
#endif

STATIC INLINE VOID TimeSliceUpdate(LosTaskCB *taskCB, UINT64 currTime)
{
    LOS_ASSERT(currTime >= taskCB->startTime);

    INT32 incTime = (currTime - taskCB->startTime - taskCB->irqUsedTime);

    LOS_ASSERT(incTime >= 0);

    if (taskCB->policy == LOS_SCHED_RR) {
        taskCB->timeSlice -= incTime;
#ifdef LOSCFG_SCHED_DEBUG
        taskCB->schedStat.timeSliceRealTime += incTime;
#endif
    }
    taskCB->irqUsedTime = 0;
    taskCB->startTime = currTime;

#ifdef LOSCFG_SCHED_DEBUG
    taskCB->schedStat.allRuntime += incTime;
#endif
}

STATIC INLINE UINT64 GetNextExpireTime(SchedRunQue *rq, UINT64 startTime, UINT32 tickPrecision)
{
    SortLinkAttribute *taskHeader = &rq->taskSortLink;
    SortLinkAttribute *swtmrHeader = &rq->swtmrSortLink;

    LOS_SpinLock(&taskHeader->spinLock);
    UINT64 taskExpireTime = OsGetSortLinkNextExpireTime(taskHeader, startTime, tickPrecision);
    LOS_SpinUnlock(&taskHeader->spinLock);

    LOS_SpinLock(&swtmrHeader->spinLock);
    UINT64 swtmrExpireTime = OsGetSortLinkNextExpireTime(swtmrHeader, startTime, tickPrecision);
    LOS_SpinUnlock(&swtmrHeader->spinLock);

    return (taskExpireTime < swtmrExpireTime) ? taskExpireTime : swtmrExpireTime;
}

STATIC INLINE VOID SchedSetNextExpireTime(UINT32 responseID, UINT64 taskEndTime, UINT32 oldResponseID)
{
    SchedRunQue *rq = OsSchedRunQue();
    BOOL isTimeSlice = FALSE;
    UINT64 currTime = OsGetCurrSchedTimeCycle();
    UINT64 nextExpireTime = GetNextExpireTime(rq, currTime, OS_TICK_RESPONSE_PRECISION);

    rq->schedFlag &= ~INT_PEND_TICK;
    if (rq->responseID == oldResponseID) {
        /* This time has expired, and the next time the theory has expired is infinite */
        rq->responseTime = OS_SCHED_MAX_RESPONSE_TIME;
    }

    /* The current thread's time slice has been consumed, but the current system lock task cannot
     * trigger the schedule to release the CPU
     */
    if ((nextExpireTime > taskEndTime) && ((nextExpireTime - taskEndTime) > OS_SCHED_MINI_PERIOD)) {
        nextExpireTime = taskEndTime;
        isTimeSlice = TRUE;
    }

    if ((rq->responseTime <= nextExpireTime) ||
        ((rq->responseTime - nextExpireTime) < OS_TICK_RESPONSE_PRECISION)) {
        return;
    }

    if (isTimeSlice) {
        /* The expiration time of the current system is the thread's slice expiration time */
        rq->responseID = responseID;
    } else {
        rq->responseID = OS_INVALID_VALUE;
    }

    UINT64 nextResponseTime = nextExpireTime - currTime;
    rq->responseTime = currTime + HalClockTickTimerReload(nextResponseTime);

#ifdef LOSCFG_SCHED_TICK_DEBUG
    SchedTickDebug *schedDebug = &g_schedTickDebug[ArchCurrCpuid()];
    if (schedDebug->index < OS_SCHED_DEBUG_DATA_NUM) {
        schedDebug->setTickCount++;
    }
#endif
}

VOID OsSchedUpdateExpireTime(VOID)
{
    UINT64 endTime;
    LosTaskCB *runTask = OsCurrTaskGet();

    if (!OS_SCHEDULER_ACTIVE || OS_INT_ACTIVE) {
        OsSchedRunQuePendingSet();
        return;
    }

    if (runTask->policy == LOS_SCHED_RR) {
        LOS_SpinLock(&g_taskSpin);
        INT32 timeSlice = (runTask->timeSlice <= OS_TIME_SLICE_MIN) ? runTask->initTimeSlice : runTask->timeSlice;
        endTime = runTask->startTime + timeSlice;
        LOS_SpinUnlock(&g_taskSpin);
    } else {
        endTime = OS_SCHED_MAX_RESPONSE_TIME - OS_TICK_RESPONSE_PRECISION;
    }

    SchedSetNextExpireTime(runTask->taskID, endTime, runTask->taskID);
}

STATIC INLINE UINT32 SchedCalculateTimeSlice(UINT16 proPriority, UINT16 priority)
{
    UINT32 retTime;
    UINT32 readyTasks;

    SchedQueue *queueList = &g_sched.queueList[proPriority];
    readyTasks = queueList->readyTasks[priority];
    if (readyTasks > OS_SCHED_READY_MAX) {
        return OS_SCHED_TIME_SLICES_MIN;
    }
    retTime = ((OS_SCHED_READY_MAX - readyTasks) * OS_SCHED_TIME_SLICES_DIFF) / OS_SCHED_READY_MAX;
    return (retTime + OS_SCHED_TIME_SLICES_MIN);
}

STATIC INLINE VOID SchedPriQueueEnHead(UINT32 proPriority, LOS_DL_LIST *priqueueItem, UINT32 priority)
{
    SchedQueue *queueList = &g_sched.queueList[proPriority];
    LOS_DL_LIST *priQueueList = &queueList->priQueueList[0];
    UINT32 *bitMap = &queueList->queueBitmap;

    /*
     * Task control blocks are inited as zero. And when task is deleted,
     * and at the same time would be deleted from priority queue or
     * other lists, task pend node will restored as zero.
     */
    LOS_ASSERT(priqueueItem->pstNext == NULL);

    if (*bitMap == 0) {
        g_sched.queueBitmap |= PRIQUEUE_PRIOR0_BIT >> proPriority;
    }

    if (LOS_ListEmpty(&priQueueList[priority])) {
        *bitMap |= PRIQUEUE_PRIOR0_BIT >> priority;
    }

    LOS_ListHeadInsert(&priQueueList[priority], priqueueItem);
    queueList->readyTasks[priority]++;
}

STATIC INLINE VOID SchedPriQueueEnTail(UINT32 proPriority, LOS_DL_LIST *priqueueItem, UINT32 priority)
{
    SchedQueue *queueList = &g_sched.queueList[proPriority];
    LOS_DL_LIST *priQueueList = &queueList->priQueueList[0];
    UINT32 *bitMap = &queueList->queueBitmap;

    /*
     * Task control blocks are inited as zero. And when task is deleted,
     * and at the same time would be deleted from priority queue or
     * other lists, task pend node will restored as zero.
     */
    LOS_ASSERT(priqueueItem->pstNext == NULL);

    if (*bitMap == 0) {
        g_sched.queueBitmap |= PRIQUEUE_PRIOR0_BIT >> proPriority;
    }

    if (LOS_ListEmpty(&priQueueList[priority])) {
        *bitMap |= PRIQUEUE_PRIOR0_BIT >> priority;
    }

    LOS_ListTailInsert(&priQueueList[priority], priqueueItem);
    queueList->readyTasks[priority]++;
}

STATIC INLINE VOID SchedPriQueueDelete(UINT32 proPriority, LOS_DL_LIST *priqueueItem, UINT32 priority)
{
    SchedQueue *queueList = &g_sched.queueList[proPriority];
    LOS_DL_LIST *priQueueList = &queueList->priQueueList[0];
    UINT32 *bitMap = &queueList->queueBitmap;

    LOS_ListDelete(priqueueItem);
    queueList->readyTasks[priority]--;
    if (LOS_ListEmpty(&priQueueList[priority])) {
        *bitMap &= ~(PRIQUEUE_PRIOR0_BIT >> priority);
    }

    if (*bitMap == 0) {
        g_sched.queueBitmap &= ~(PRIQUEUE_PRIOR0_BIT >> proPriority);
    }
}

STATIC INLINE VOID SchedEnTaskQueue(LosTaskCB *taskCB)
{
    LOS_ASSERT(!(taskCB->taskStatus & OS_TASK_STATUS_READY));

    switch (taskCB->policy) {
        case LOS_SCHED_RR: {
            if (taskCB->timeSlice > OS_TIME_SLICE_MIN) {
                SchedPriQueueEnHead(taskCB->basePrio, &taskCB->pendList, taskCB->priority);
            } else {
                taskCB->initTimeSlice = SchedCalculateTimeSlice(taskCB->basePrio, taskCB->priority);
                taskCB->timeSlice = taskCB->initTimeSlice;
                SchedPriQueueEnTail(taskCB->basePrio, &taskCB->pendList, taskCB->priority);
#ifdef LOSCFG_SCHED_DEBUG
                taskCB->schedStat.timeSliceTime = taskCB->schedStat.timeSliceRealTime;
                taskCB->schedStat.timeSliceCount++;
#endif
            }
            break;
        }
        case LOS_SCHED_FIFO: {
            /* The time slice of FIFO is always greater than 0 unless the yield is called */
            if ((taskCB->timeSlice > OS_TIME_SLICE_MIN) && (taskCB->taskStatus & OS_TASK_STATUS_RUNNING)) {
                SchedPriQueueEnHead(taskCB->basePrio, &taskCB->pendList, taskCB->priority);
            } else {
                taskCB->initTimeSlice = OS_SCHED_FIFO_TIMEOUT;
                taskCB->timeSlice = taskCB->initTimeSlice;
                SchedPriQueueEnTail(taskCB->basePrio, &taskCB->pendList, taskCB->priority);
            }
            break;
        }
        case LOS_SCHED_IDLE:
#ifdef LOSCFG_SCHED_DEBUG
            taskCB->schedStat.timeSliceCount = 1;
#endif
            break;
        default:
            LOS_ASSERT(0);
            break;
    }

    taskCB->taskStatus &= ~OS_TASK_STATUS_BLOCKED;
    taskCB->taskStatus |= OS_TASK_STATUS_READY;
}

VOID OsSchedTaskDeQueue(LosTaskCB *taskCB)
{
    if (taskCB->taskStatus & OS_TASK_STATUS_READY) {
        if (taskCB->policy != LOS_SCHED_IDLE) {
            SchedPriQueueDelete(taskCB->basePrio, &taskCB->pendList, taskCB->priority);
        }
        taskCB->taskStatus &= ~OS_TASK_STATUS_READY;
    }
}

VOID OsSchedTaskEnQueue(LosTaskCB *taskCB)
{
#ifdef LOSCFG_SCHED_DEBUG
    if (!(taskCB->taskStatus & OS_TASK_STATUS_RUNNING)) {
        taskCB->startTime = OsGetCurrSchedTimeCycle();
    }
#endif
    SchedEnTaskQueue(taskCB);
}

VOID OsSchedTaskExit(LosTaskCB *taskCB)
{
    if (taskCB->taskStatus & OS_TASK_STATUS_READY) {
        OsSchedTaskDeQueue(taskCB);
    } else if (taskCB->taskStatus & OS_TASK_STATUS_PENDING) {
        LOS_ListDelete(&taskCB->pendList);
        taskCB->taskStatus &= ~OS_TASK_STATUS_PENDING;
    }

    if (taskCB->taskStatus & (OS_TASK_STATUS_DELAY | OS_TASK_STATUS_PEND_TIME)) {
        OsSchedDeTaskFromTimeList(&taskCB->sortList);
        taskCB->taskStatus &= ~(OS_TASK_STATUS_DELAY | OS_TASK_STATUS_PEND_TIME);
    }
}

VOID OsSchedYield(VOID)
{
    LosTaskCB *runTask = OsCurrTaskGet();

    runTask->timeSlice = 0;

    runTask->startTime = OsGetCurrSchedTimeCycle();
    OsSchedTaskEnQueue(runTask);
    OsSchedResched();
}

VOID OsSchedDelay(LosTaskCB *runTask, UINT32 tick)
{
    OsSchedTaskDeQueue(runTask);
    runTask->taskStatus |= OS_TASK_STATUS_DELAY;
    runTask->waitTimes = tick;

    OsSchedResched();
}

UINT32 OsSchedTaskWait(LOS_DL_LIST *list, UINT32 ticks, BOOL needSched)
{
    LosTaskCB *runTask = OsCurrTaskGet();

    runTask->taskStatus |= OS_TASK_STATUS_PENDING;
    LOS_ListTailInsert(list, &runTask->pendList);

    if (ticks != LOS_WAIT_FOREVER) {
        runTask->taskStatus |= OS_TASK_STATUS_PEND_TIME;
        runTask->waitTimes = ticks;
    }

    if (needSched == TRUE) {
        OsSchedResched();
        if (runTask->taskStatus & OS_TASK_STATUS_TIMEOUT) {
            runTask->taskStatus &= ~OS_TASK_STATUS_TIMEOUT;
            return LOS_ERRNO_TSK_TIMEOUT;
        }
    }

    return LOS_OK;
}

VOID OsSchedTaskWake(LosTaskCB *resumedTask)
{
    LOS_ListDelete(&resumedTask->pendList);
    resumedTask->taskStatus &= ~OS_TASK_STATUS_PENDING;

    if (resumedTask->taskStatus & OS_TASK_STATUS_PEND_TIME) {
        OsSchedDeTaskFromTimeList(&resumedTask->sortList);
        resumedTask->taskStatus &= ~OS_TASK_STATUS_PEND_TIME;
    }

    if (!(resumedTask->taskStatus & OS_TASK_STATUS_SUSPENDED)) {
#ifdef LOSCFG_SCHED_DEBUG
        resumedTask->schedStat.pendTime += OsGetCurrSchedTimeCycle() - resumedTask->startTime;
        resumedTask->schedStat.pendCount++;
#endif
        OsSchedTaskEnQueue(resumedTask);
    }
}

BOOL OsSchedModifyTaskSchedParam(LosTaskCB *taskCB, UINT16 policy, UINT16 priority)
{
    if (taskCB->policy != policy) {
        taskCB->policy = policy;
        taskCB->timeSlice = 0;
    }

    if (taskCB->taskStatus & OS_TASK_STATUS_READY) {
        OsSchedTaskDeQueue(taskCB);
        taskCB->priority = priority;
        OsSchedTaskEnQueue(taskCB);
        return TRUE;
    }

    taskCB->priority = priority;
    OsHookCall(LOS_HOOK_TYPE_TASK_PRIMODIFY, taskCB, taskCB->priority);
    if (taskCB->taskStatus & OS_TASK_STATUS_INIT) {
        OsSchedTaskEnQueue(taskCB);
        return TRUE;
    }

    if (taskCB->taskStatus & OS_TASK_STATUS_RUNNING) {
        return TRUE;
    }

    return FALSE;
}

BOOL OsSchedModifyProcessSchedParam(UINT32 pid, UINT16 policy, UINT16 priority)
{
    LosProcessCB *processCB = OS_PCB_FROM_PID(pid);
    LosTaskCB *taskCB = NULL;
    BOOL needSched = FALSE;
    (VOID)policy;

    LOS_DL_LIST_FOR_EACH_ENTRY(taskCB, &processCB->threadSiblingList, LosTaskCB, threadList) {
        if (taskCB->taskStatus & OS_TASK_STATUS_READY) {
            SchedPriQueueDelete(taskCB->basePrio, &taskCB->pendList, taskCB->priority);
            SchedPriQueueEnTail(priority, &taskCB->pendList, taskCB->priority);
            needSched = TRUE;
        } else if (taskCB->taskStatus & OS_TASK_STATUS_RUNNING) {
            needSched = TRUE;
        }
        taskCB->basePrio = priority;
    }

    return needSched;
}

STATIC VOID SchedFreezeTask(LosTaskCB *taskCB)
{
    UINT64 responseTime;

    if (!OsIsPmMode()) {
        return;
    }

    if (!(taskCB->taskStatus & (OS_TASK_STATUS_PEND_TIME | OS_TASK_STATUS_DELAY))) {
        return;
    }

    responseTime = GET_SORTLIST_VALUE(&taskCB->sortList);
    OsSchedDeTaskFromTimeList(&taskCB->sortList);
    SET_SORTLIST_VALUE(&taskCB->sortList, responseTime);
    taskCB->taskStatus |= OS_TASK_FLAG_FREEZE;
    return;
}

STATIC VOID SchedUnfreezeTask(LosTaskCB *taskCB)
{
    UINT64 currTime, responseTime;
    UINT32 remainTick;

    if (!(taskCB->taskStatus & OS_TASK_FLAG_FREEZE)) {
        return;
    }

    taskCB->taskStatus &= ~OS_TASK_FLAG_FREEZE;
    currTime = OsGetCurrSchedTimeCycle();
    responseTime = GET_SORTLIST_VALUE(&taskCB->sortList);
    if (responseTime > currTime) {
        remainTick = ((responseTime - currTime) + OS_CYCLE_PER_TICK - 1) / OS_CYCLE_PER_TICK;
        OsSchedAddTask2TimeList(&taskCB->sortList, currTime, remainTick);
        return;
    }

    SET_SORTLIST_VALUE(&taskCB->sortList, OS_SORT_LINK_INVALID_TIME);
    if (taskCB->taskStatus & OS_TASK_STATUS_PENDING) {
        LOS_ListDelete(&taskCB->pendList);
    }
    taskCB->taskStatus &= ~(OS_TASK_STATUS_DELAY | OS_TASK_STATUS_PEND_TIME | OS_TASK_STATUS_PENDING);
    return;
}

VOID OsSchedSuspend(LosTaskCB *taskCB)
{
    if (taskCB->taskStatus & OS_TASK_STATUS_READY) {
        OsSchedTaskDeQueue(taskCB);
    }

    SchedFreezeTask(taskCB);

    taskCB->taskStatus |= OS_TASK_STATUS_SUSPENDED;
    OsHookCall(LOS_HOOK_TYPE_MOVEDTASKTOSUSPENDEDLIST, taskCB);
    if (taskCB == OsCurrTaskGet()) {
        OsSchedResched();
    }
}

BOOL OsSchedResume(LosTaskCB *taskCB)
{
    BOOL needSched = FALSE;

    SchedUnfreezeTask(taskCB);

    taskCB->taskStatus &= ~OS_TASK_STATUS_SUSPENDED;
    if (!OsTaskIsBlocked(taskCB)) {
        OsSchedTaskEnQueue(taskCB);
        needSched = TRUE;
    }

    return needSched;
}

STATIC INLINE BOOL SchedScanSwtmrTimeList(SchedRunQue *rq)
{
    BOOL needSched = FALSE;
    SortLinkAttribute* swtmrSortLink = &rq->swtmrSortLink;
    LOS_DL_LIST *listObject = &swtmrSortLink->sortLink;

    /*
     * it needs to be carefully coped with, since the swtmr is in specific sortlink
     * while other cores still has the chance to process it, like stop the timer.
     */
    LOS_SpinLock(&swtmrSortLink->spinLock);

    if (LOS_ListEmpty(listObject)) {
        LOS_SpinUnlock(&swtmrSortLink->spinLock);
        return FALSE;
    }
    SortLinkList *sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);

    UINT64 currTime = OsGetCurrSchedTimeCycle();
    while (sortList->responseTime <= currTime) {
        sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
        UINT64 startTime = GET_SORTLIST_VALUE(sortList);
        OsDeleteNodeSortLink(swtmrSortLink, sortList);
        LOS_SpinUnlock(&swtmrSortLink->spinLock);

        OsSwtmrWake(rq, startTime, sortList);
        needSched = TRUE;

        LOS_SpinLock(&swtmrSortLink->spinLock);
        if (LOS_ListEmpty(listObject)) {
            break;
        }

        sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
    }

    LOS_SpinUnlock(&swtmrSortLink->spinLock);
    return needSched;
}

STATIC INLINE VOID SchedSwtmrResponseTimeReset(SchedRunQue *rq, UINT64 startTime)
{
    SortLinkAttribute* swtmrSortLink = &rq->swtmrSortLink;
    LOS_DL_LIST *listHead = &swtmrSortLink->sortLink;
    LOS_DL_LIST *listNext = listHead->pstNext;

    LOS_SpinLock(&swtmrSortLink->spinLock);
    while (listNext != listHead) {
        SortLinkList *sortList = LOS_DL_LIST_ENTRY(listNext, SortLinkList, sortLinkNode);
        OsDeleteNodeSortLink(swtmrSortLink, sortList);
        LOS_SpinUnlock(&swtmrSortLink->spinLock);

        OsSwtmrRestart(startTime, sortList);

        LOS_SpinLock(&swtmrSortLink->spinLock);
        listNext = listNext->pstNext;
    }
    LOS_SpinUnlock(&swtmrSortLink->spinLock);
}

STATIC INLINE BOOL SchedSwtmrRunQueFind(SortLinkAttribute *swtmrSortLink, SCHED_TL_FIND_FUNC checkFunc, UINTPTR arg)
{
    LOS_DL_LIST *listObject = &swtmrSortLink->sortLink;
    LOS_DL_LIST *list = listObject->pstNext;

    LOS_SpinLock(&swtmrSortLink->spinLock);
    while (list != listObject) {
        SortLinkList *listSorted = LOS_DL_LIST_ENTRY(list, SortLinkList, sortLinkNode);
        if (checkFunc((UINTPTR)listSorted, arg)) {
            LOS_SpinUnlock(&swtmrSortLink->spinLock);
            return TRUE;
        }
        list = list->pstNext;
    }

    LOS_SpinUnlock(&swtmrSortLink->spinLock);
    return FALSE;
}

BOOL OsSchedSwtmrTimeListFind(SCHED_TL_FIND_FUNC checkFunc, UINTPTR arg)
{
    for (UINT16 cpuid = 0; cpuid < LOSCFG_KERNEL_CORE_NUM; cpuid++) {
        SchedRunQue *rq = OsSchedRunQueByID(cpuid);
        SortLinkAttribute *swtmrSortLink = &rq->swtmrSortLink;
        if (SchedSwtmrRunQueFind(swtmrSortLink, checkFunc, arg)) {
            return TRUE;
        }
    }
    return FALSE;
}

STATIC INLINE VOID SchedWakePendTimeTask(UINT64 currTime, LosTaskCB *taskCB, BOOL *needSchedule)
{
#ifndef LOSCFG_SCHED_DEBUG
    (VOID)currTime;
#endif

    LOS_SpinLock(&g_taskSpin);
    UINT16 tempStatus = taskCB->taskStatus;
    if (tempStatus & (OS_TASK_STATUS_PENDING | OS_TASK_STATUS_DELAY)) {
        taskCB->taskStatus &= ~(OS_TASK_STATUS_PENDING | OS_TASK_STATUS_PEND_TIME | OS_TASK_STATUS_DELAY);
        if (tempStatus & OS_TASK_STATUS_PENDING) {
            taskCB->taskStatus |= OS_TASK_STATUS_TIMEOUT;
            LOS_ListDelete(&taskCB->pendList);
            taskCB->taskMux = NULL;
            OsTaskWakeClearPendMask(taskCB);
        }

        if (!(tempStatus & OS_TASK_STATUS_SUSPENDED)) {
#ifdef LOSCFG_SCHED_DEBUG
            taskCB->schedStat.pendTime += currTime - taskCB->startTime;
            taskCB->schedStat.pendCount++;
#endif
            OsSchedTaskEnQueue(taskCB);
            *needSchedule = TRUE;
        }
    }

    LOS_SpinUnlock(&g_taskSpin);
}

STATIC INLINE BOOL SchedScanTaskTimeList(SchedRunQue *rq)
{
    BOOL needSchedule = FALSE;
    SortLinkAttribute *taskSortLink = &rq->taskSortLink;
    LOS_DL_LIST *listObject = &taskSortLink->sortLink;
    /*
     * When task is pended with timeout, the task block is on the timeout sortlink
     * (per cpu) and ipc(mutex,sem and etc.)'s block at the same time, it can be waken
     * up by either timeout or corresponding ipc it's waiting.
     *
     * Now synchronize sortlink procedure is used, therefore the whole task scan needs
     * to be protected, preventing another core from doing sortlink deletion at same time.
     */
    LOS_SpinLock(&taskSortLink->spinLock);

    if (LOS_ListEmpty(listObject)) {
        LOS_SpinUnlock(&taskSortLink->spinLock);
        return needSchedule;
    }

    SortLinkList *sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
    UINT64 currTime = OsGetCurrSchedTimeCycle();
    while (sortList->responseTime <= currTime) {
        LosTaskCB *taskCB = LOS_DL_LIST_ENTRY(sortList, LosTaskCB, sortList);
        OsDeleteNodeSortLink(taskSortLink, &taskCB->sortList);
        LOS_SpinUnlock(&taskSortLink->spinLock);

        SchedWakePendTimeTask(currTime, taskCB, &needSchedule);

        LOS_SpinLock(&taskSortLink->spinLock);
        if (LOS_ListEmpty(listObject)) {
            break;
        }

        sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
    }

    LOS_SpinUnlock(&taskSortLink->spinLock);

    return needSchedule;
}

VOID OsSchedTick(VOID)
{
    SchedRunQue *rq = OsSchedRunQue();
    BOOL needSched = FALSE;

    if (rq->responseID == OS_INVALID_VALUE) {

        needSched |= SchedScanSwtmrTimeList(rq);
        needSched |= SchedScanTaskTimeList(rq);

        if (needSched) {
            LOS_MpSchedule(OS_MP_CPU_ALL);
            rq->schedFlag |= INT_PEND_RESCH;
        }
    }
    rq->schedFlag |= INT_PEND_TICK;
    rq->responseTime = OS_SCHED_MAX_RESPONSE_TIME;
}

VOID OsSchedSetIdleTaskSchedParam(LosTaskCB *idleTask)
{
    idleTask->basePrio = OS_TASK_PRIORITY_LOWEST;
    idleTask->policy = LOS_SCHED_IDLE;
    idleTask->initTimeSlice = OS_SCHED_FIFO_TIMEOUT;
    idleTask->timeSlice = idleTask->initTimeSlice;
    OsSchedTaskEnQueue(idleTask);
}

VOID OsSchedResetSchedResponseTime(UINT64 responseTime)
{
    OsSchedRunQue()->responseTime = responseTime;
}

VOID OsSchedRunQueInit(VOID)
{
    if (ArchCurrCpuid() != 0) {
        return;
    }

    for (UINT16 index = 0; index < LOSCFG_KERNEL_CORE_NUM; index++) {
        SchedRunQue *rq = OsSchedRunQueByID(index);
        OsSortLinkInit(&rq->taskSortLink);
        OsSortLinkInit(&rq->swtmrSortLink);
        rq->responseTime = OS_SCHED_MAX_RESPONSE_TIME;
    }
}

VOID OsSchedRunQueSwtmrInit(UINT32 swtmrTaskID, UINT32 swtmrQueue)
{
    SchedRunQue *rq = OsSchedRunQue();
    rq->swtmrTaskID = swtmrTaskID;
    rq->swtmrHandlerQueue = swtmrQueue;
}

VOID OsSchedRunQueIdleInit(UINT32 idleTaskID)
{
    SchedRunQue *rq = OsSchedRunQue();
    rq->idleTaskID = idleTaskID;
}

UINT32 OsSchedInit(VOID)
{
    for (UINT16 index = 0; index < OS_PRIORITY_QUEUE_NUM; index++) {
        SchedQueue *queueList = &g_sched.queueList[index];
        LOS_DL_LIST *priList = &queueList->priQueueList[0];
        for (UINT16 pri = 0; pri < OS_PRIORITY_QUEUE_NUM; pri++) {
            LOS_ListInit(&priList[pri]);
        }
    }

#ifdef LOSCFG_SCHED_TICK_DEBUG
    UINT32 ret = OsSchedDebugInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif
    return LOS_OK;
}

STATIC LosTaskCB *GetTopTask(SchedRunQue *rq)
{
    UINT32 priority, processPriority;
    UINT32 bitmap;
    LosTaskCB *newTask = NULL;
    UINT32 processBitmap = g_sched.queueBitmap;
#ifdef LOSCFG_KERNEL_SMP
    UINT32 cpuid = ArchCurrCpuid();
#endif

    while (processBitmap) {
        processPriority = CLZ(processBitmap);
        SchedQueue *queueList = &g_sched.queueList[processPriority];
        bitmap = queueList->queueBitmap;
            while (bitmap) {
                priority = CLZ(bitmap);
                LOS_DL_LIST_FOR_EACH_ENTRY(newTask, &queueList->priQueueList[priority], LosTaskCB, pendList) {
#ifdef LOSCFG_KERNEL_SMP
                    if (newTask->cpuAffiMask & (1U << cpuid)) {
#endif
                        goto FIND_TASK;
#ifdef LOSCFG_KERNEL_SMP
                    }
#endif
                }
            bitmap &= ~(1U << (OS_PRIORITY_QUEUE_NUM - priority - 1));
        }
        processBitmap &= ~(1U << (OS_PRIORITY_QUEUE_NUM - processPriority - 1));
    }

    newTask = OS_TCB_FROM_TID(rq->idleTaskID);

FIND_TASK:
    OsSchedTaskDeQueue(newTask);
    return newTask;
}

VOID OsSchedStart(VOID)
{
    UINT32 cpuid = ArchCurrCpuid();
    UINT32 intSave;

    PRINTK("cpu %d entering scheduler\n", cpuid);

    SCHEDULER_LOCK(intSave);

    if (cpuid == 0) {
        OsTickStart();
    }

    SchedRunQue *rq = OsSchedRunQue();
    LosTaskCB *newTask = GetTopTask(rq);
    newTask->taskStatus |= OS_TASK_STATUS_RUNNING;

#ifdef LOSCFG_KERNEL_SMP
    /*
     * attention: current cpu needs to be set, in case first task deletion
     * may fail because this flag mismatch with the real current cpu.
     */
    newTask->currCpu = cpuid;
#endif

    OsCurrTaskSet((VOID *)newTask);

    newTask->startTime = OsGetCurrSchedTimeCycle();

    SchedSwtmrResponseTimeReset(rq, newTask->startTime);

    /* System start schedule */
    OS_SCHEDULER_SET(cpuid);

    rq->responseID = OS_INVALID;
    SchedSetNextExpireTime(newTask->taskID, newTask->startTime + newTask->timeSlice, OS_INVALID);
    OsTaskContextLoad(newTask);
}

#ifdef LOSCFG_KERNEL_SMP
VOID OsSchedToUserReleaseLock(VOID)
{
    /* The scheduling lock needs to be released before returning to user mode */
    LOCKDEP_CHECK_OUT(&g_taskSpin);
    ArchSpinUnlock(&g_taskSpin.rawLock);

    OsSchedUnlock();
}
#endif

#ifdef LOSCFG_BASE_CORE_TSK_MONITOR
STATIC VOID TaskStackCheck(LosTaskCB *runTask, LosTaskCB *newTask)
{
    if (!OS_STACK_MAGIC_CHECK(runTask->topOfStack)) {
        LOS_Panic("CURRENT task ID: %s:%d stack overflow!\n", runTask->taskName, runTask->taskID);
    }

    if (((UINTPTR)(newTask->stackPointer) <= newTask->topOfStack) ||
        ((UINTPTR)(newTask->stackPointer) > (newTask->topOfStack + newTask->stackSize))) {
        LOS_Panic("HIGHEST task ID: %s:%u SP error! StackPointer: %p TopOfStack: %p\n",
                  newTask->taskName, newTask->taskID, newTask->stackPointer, newTask->topOfStack);
    }
}
#endif

STATIC INLINE VOID SchedSwitchCheck(LosTaskCB *runTask, LosTaskCB *newTask)
{
#ifdef LOSCFG_BASE_CORE_TSK_MONITOR
    TaskStackCheck(runTask, newTask);
#endif /* LOSCFG_BASE_CORE_TSK_MONITOR */
    OsHookCall(LOS_HOOK_TYPE_TASK_SWITCHEDIN, newTask, runTask);
}

STATIC VOID SchedTaskSwitch(LosTaskCB *runTask, LosTaskCB *newTask)
{
    UINT64 endTime;

    SchedSwitchCheck(runTask, newTask);

    runTask->taskStatus &= ~OS_TASK_STATUS_RUNNING;
    newTask->taskStatus |= OS_TASK_STATUS_RUNNING;

#ifdef LOSCFG_KERNEL_SMP
    /* mask new running task's owner processor */
    runTask->currCpu = OS_TASK_INVALID_CPUID;
    newTask->currCpu = ArchCurrCpuid();
#endif

    OsCurrTaskSet((VOID *)newTask);
#ifdef LOSCFG_KERNEL_VM
    if (newTask->archMmu != runTask->archMmu) {
        LOS_ArchMmuContextSwitch((LosArchMmu *)newTask->archMmu);
    }
#endif

#ifdef LOSCFG_KERNEL_CPUP
    OsCpupCycleEndStart(runTask->taskID, newTask->taskID);
#endif

#ifdef LOSCFG_SCHED_DEBUG
    UINT64 waitStartTime = newTask->startTime;
#endif
    if (runTask->taskStatus & OS_TASK_STATUS_READY) {
        /* When a thread enters the ready queue, its slice of time is updated */
        newTask->startTime = runTask->startTime;
    } else {
        /* The currently running task is blocked */
        newTask->startTime = OsGetCurrSchedTimeCycle();
        /* The task is in a blocking state and needs to update its time slice before pend */
        TimeSliceUpdate(runTask, newTask->startTime);

        if (runTask->taskStatus & (OS_TASK_STATUS_PEND_TIME | OS_TASK_STATUS_DELAY)) {
            OsSchedAddTask2TimeList(&runTask->sortList, runTask->startTime, runTask->waitTimes);
        }
    }

    if (newTask->policy == LOS_SCHED_RR) {
        endTime = newTask->startTime + newTask->timeSlice;
    } else {
        endTime = OS_SCHED_MAX_RESPONSE_TIME - OS_TICK_RESPONSE_PRECISION;
    }
    SchedSetNextExpireTime(newTask->taskID, endTime, runTask->taskID);

#ifdef LOSCFG_SCHED_DEBUG
    newTask->schedStat.waitSchedTime += newTask->startTime - waitStartTime;
    newTask->schedStat.waitSchedCount++;
    runTask->schedStat.runTime = runTask->schedStat.allRuntime;
    runTask->schedStat.switchCount++;
#endif
    /* do the task context switch */
    OsTaskSchedule(newTask, runTask);
}

VOID OsSchedIrqEndCheckNeedSched(VOID)
{
    SchedRunQue *rq = OsSchedRunQue();
    LosTaskCB *runTask = OsCurrTaskGet();

    TimeSliceUpdate(runTask, OsGetCurrSchedTimeCycle());
    if (runTask->timeSlice <= OS_TIME_SLICE_MIN) {
        rq->schedFlag |= INT_PEND_RESCH;
    }

    if (OsPreemptable() && (rq->schedFlag & INT_PEND_RESCH)) {
        rq->schedFlag &= ~INT_PEND_RESCH;

        LOS_SpinLock(&g_taskSpin);

        OsSchedTaskEnQueue(runTask);

        LosTaskCB *newTask = GetTopTask(rq);
        if (runTask != newTask) {
            SchedTaskSwitch(runTask, newTask);
            LOS_SpinUnlock(&g_taskSpin);
            return;
        }

        LOS_SpinUnlock(&g_taskSpin);
    }

    if (rq->schedFlag & INT_PEND_TICK) {
        OsSchedUpdateExpireTime();
    }
}

VOID OsSchedResched(VOID)
{
    LOS_ASSERT(LOS_SpinHeld(&g_taskSpin));
    SchedRunQue *rq = OsSchedRunQue();
#ifdef LOSCFG_KERNEL_SMP
    LOS_ASSERT(rq->taskLockCnt == 1);
#else
    LOS_ASSERT(rq->taskLockCnt == 0);
#endif

    rq->schedFlag &= ~INT_PEND_RESCH;
    LosTaskCB *runTask = OsCurrTaskGet();
    LosTaskCB *newTask = GetTopTask(rq);
    if (runTask == newTask) {
        return;
    }

    SchedTaskSwitch(runTask, newTask);
}

VOID LOS_Schedule(VOID)
{
    UINT32 intSave;
    LosTaskCB *runTask = OsCurrTaskGet();

    if (OS_INT_ACTIVE) {
        OsSchedRunQuePendingSet();
        return;
    }

    if (!OsPreemptable()) {
        return;
    }

    /*
     * trigger schedule in task will also do the slice check
     * if necessary, it will give up the timeslice more in time.
     * otherwise, there's no other side effects.
     */
    SCHEDULER_LOCK(intSave);

    TimeSliceUpdate(runTask, OsGetCurrSchedTimeCycle());

    /* add run task back to ready queue */
    OsSchedTaskEnQueue(runTask);

    /* reschedule to new thread */
    OsSchedResched();

    SCHEDULER_UNLOCK(intSave);
}

STATIC INLINE LOS_DL_LIST *OsSchedLockPendFindPosSub(const LosTaskCB *runTask, const LOS_DL_LIST *lockList)
{
    LosTaskCB *pendedTask = NULL;
    LOS_DL_LIST *node = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY(pendedTask, lockList, LosTaskCB, pendList) {
        if (pendedTask->priority < runTask->priority) {
            continue;
        } else if (pendedTask->priority > runTask->priority) {
            node = &pendedTask->pendList;
            break;
        } else {
            node = pendedTask->pendList.pstNext;
            break;
        }
    }

    return node;
}

LOS_DL_LIST *OsSchedLockPendFindPos(const LosTaskCB *runTask, LOS_DL_LIST *lockList)
{
    LOS_DL_LIST *node = NULL;

    if (LOS_ListEmpty(lockList)) {
        node = lockList;
    } else {
        LosTaskCB *pendedTask1 = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(lockList));
        LosTaskCB *pendedTask2 = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_LAST(lockList));
        if (pendedTask1->priority > runTask->priority) {
            node = lockList->pstNext;
        } else if (pendedTask2->priority <= runTask->priority) {
            node = lockList;
        } else {
            node = OsSchedLockPendFindPosSub(runTask, lockList);
        }
    }

    return node;
}

