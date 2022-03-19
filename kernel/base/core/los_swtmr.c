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

#include "los_swtmr_pri.h"
#include "los_init.h"
#include "los_process_pri.h"
#include "los_queue_pri.h"
#include "los_sched_pri.h"
#include "los_sortlink_pri.h"
#include "los_task_pri.h"
#include "los_hook.h"

#ifdef LOSCFG_BASE_CORE_SWTMR_ENABLE
#if (LOSCFG_BASE_CORE_SWTMR_LIMIT <= 0)
#error "swtmr maxnum cannot be zero"
#endif /* LOSCFG_BASE_CORE_SWTMR_LIMIT <= 0 */

STATIC INLINE VOID SwtmrDelete(SWTMR_CTRL_S *swtmr);
STATIC INLINE UINT64 SwtmrToStart(SWTMR_CTRL_S *swtmr, UINT16 cpuid);

LITE_OS_SEC_BSS SWTMR_CTRL_S    *g_swtmrCBArray = NULL;     /* First address in Timer memory space */
LITE_OS_SEC_BSS UINT8           *g_swtmrHandlerPool = NULL; /* Pool of Swtmr Handler */
LITE_OS_SEC_BSS LOS_DL_LIST     g_swtmrFreeList;            /* Free list of Software Timer */

/* spinlock for swtmr module, only available on SMP mode */
LITE_OS_SEC_BSS  SPIN_LOCK_INIT(g_swtmrSpin);
#define SWTMR_LOCK(state)       LOS_SpinLockSave(&g_swtmrSpin, &(state))
#define SWTMR_UNLOCK(state)     LOS_SpinUnlockRestore(&g_swtmrSpin, (state))

typedef struct {
    SortLinkAttribute swtmrSortLink;
    LosTaskCB         *swtmrTask;           /* software timer task id */
    LOS_DL_LIST       swtmrHandlerQueue;     /* software timer timeout queue id */
} SwtmrRunQue;

STATIC SwtmrRunQue g_swtmrRunQue[LOSCFG_KERNEL_CORE_NUM];

#ifdef LOSCFG_SWTMR_DEBUG
#define OS_SWTMR_PERIOD_TO_CYCLE(period) (((UINT64)(period) * OS_NS_PER_TICK) / OS_NS_PER_CYCLE)
STATIC SwtmrDebugData *g_swtmrDebugData = NULL;

BOOL OsSwtmrDebugDataUsed(UINT32 swtmrID)
{
    if (swtmrID > LOSCFG_BASE_CORE_SWTMR_LIMIT) {
        return FALSE;
    }

    return g_swtmrDebugData[swtmrID].swtmrUsed;
}

UINT32 OsSwtmrDebugDataGet(UINT32 swtmrID, SwtmrDebugData *data, UINT32 len, UINT8 *mode)
{
    UINT32 intSave;
    errno_t ret;

    if ((swtmrID > LOSCFG_BASE_CORE_SWTMR_LIMIT) || (data == NULL) ||
        (mode == NULL) || (len < sizeof(SwtmrDebugData))) {
        return LOS_NOK;
    }

    SWTMR_CTRL_S *swtmr = &g_swtmrCBArray[swtmrID];
    SWTMR_LOCK(intSave);
    ret = memcpy_s(data, len, &g_swtmrDebugData[swtmrID], sizeof(SwtmrDebugData));
    *mode = swtmr->ucMode;
    SWTMR_UNLOCK(intSave);
    if (ret != EOK) {
        return LOS_NOK;
    }
    return LOS_OK;
}
#endif

STATIC VOID SwtmrDebugDataInit(VOID)
{
#ifdef LOSCFG_SWTMR_DEBUG
    UINT32 size = sizeof(SwtmrDebugData) * LOSCFG_BASE_CORE_SWTMR_LIMIT;
    g_swtmrDebugData = (SwtmrDebugData *)LOS_MemAlloc(m_aucSysMem1, size);
    if (g_swtmrDebugData == NULL) {
        PRINT_ERR("SwtmrDebugDataInit malloc failed!\n");
        return;
    }
    (VOID)memset_s(g_swtmrDebugData, size, 0, size);
#endif
}

STATIC INLINE VOID SwtmrDebugDataUpdate(SWTMR_CTRL_S *swtmr, UINT32 ticks, UINT32 times)
{
#ifdef LOSCFG_SWTMR_DEBUG
    SwtmrDebugData *data = &g_swtmrDebugData[swtmr->usTimerID];
    if (data->period != ticks) {
        (VOID)memset_s(&data->base, sizeof(SwtmrDebugBase), 0, sizeof(SwtmrDebugBase));
        data->period = ticks;
    }
    data->base.startTime = swtmr->startTime;
    data->base.times += times;
#endif
}

STATIC INLINE VOID SwtmrDebugDataStart(SWTMR_CTRL_S *swtmr, UINT16 cpuid)
{
#ifdef LOSCFG_SWTMR_DEBUG
    SwtmrDebugData *data = &g_swtmrDebugData[swtmr->usTimerID];
    data->swtmrUsed = TRUE;
    data->handler = swtmr->pfnHandler;
    data->cpuid = cpuid;
#endif
}

STATIC INLINE VOID SwtmrDebugWaitTimeCalculate(UINT32 swtmrID, SwtmrHandlerItemPtr swtmrHandler)
{
#ifdef LOSCFG_SWTMR_DEBUG
    SwtmrDebugBase *data = &g_swtmrDebugData[swtmrID].base;
    swtmrHandler->swtmrID = swtmrID;
    UINT64 currTime = OsGetCurrSchedTimeCycle();
    UINT64 waitTime = currTime - data->startTime;
    data->waitTime += waitTime;
    if (waitTime > data->waitTimeMax) {
        data->waitTimeMax = waitTime;
    }
    data->readyStartTime = currTime;
    data->waitCount++;
#endif
}

STATIC INLINE VOID SwtmrDebugDataClear(UINT32 swtmrID)
{
#ifdef LOSCFG_SWTMR_DEBUG
    (VOID)memset_s(&g_swtmrDebugData[swtmrID], sizeof(SwtmrDebugData), 0, sizeof(SwtmrDebugData));
#endif
}

STATIC INLINE VOID SwtmrHandler(SwtmrHandlerItemPtr swtmrHandle)
{
#ifdef LOSCFG_SWTMR_DEBUG
    UINT32 intSave;
    SwtmrDebugBase *data = &g_swtmrDebugData[swtmrHandle->swtmrID].base;
    UINT64 startTime = OsGetCurrSchedTimeCycle();
#endif
    swtmrHandle->handler(swtmrHandle->arg);
#ifdef LOSCFG_SWTMR_DEBUG
    UINT64 runTime = OsGetCurrSchedTimeCycle() - startTime;
    SWTMR_LOCK(intSave);
    data->runTime += runTime;
    if (runTime > data->runTimeMax) {
        data->runTimeMax = runTime;
    }
    runTime = startTime - data->readyStartTime;
    data->readyTime += runTime;
    if (runTime > data->readyTimeMax) {
        data->readyTimeMax = runTime;
    }
    data->runCount++;
    SWTMR_UNLOCK(intSave);
#endif
}

STATIC INLINE VOID SwtmrWake(SwtmrRunQue *srq, UINT64 startTime, SortLinkList *sortList)
{
    UINT32 intSave;
    SWTMR_CTRL_S *swtmr = LOS_DL_LIST_ENTRY(sortList, SWTMR_CTRL_S, stSortList);
    SwtmrHandlerItemPtr swtmrHandler = (SwtmrHandlerItemPtr)LOS_MemboxAlloc(g_swtmrHandlerPool);
    LOS_ASSERT(swtmrHandler != NULL);

    OsHookCall(LOS_HOOK_TYPE_SWTMR_EXPIRED, swtmr);

    SWTMR_LOCK(intSave);
    swtmrHandler->handler = swtmr->pfnHandler;
    swtmrHandler->arg = swtmr->uwArg;
    LOS_ListTailInsert(&srq->swtmrHandlerQueue, &swtmrHandler->node);
    SwtmrDebugWaitTimeCalculate(swtmr->usTimerID, swtmrHandler);

    if (swtmr->ucMode == LOS_SWTMR_MODE_ONCE) {
        SwtmrDelete(swtmr);

        if (swtmr->usTimerID < (OS_SWTMR_MAX_TIMERID - LOSCFG_BASE_CORE_SWTMR_LIMIT)) {
            swtmr->usTimerID += LOSCFG_BASE_CORE_SWTMR_LIMIT;
        } else {
            swtmr->usTimerID %= LOSCFG_BASE_CORE_SWTMR_LIMIT;
        }
    } else if (swtmr->ucMode == LOS_SWTMR_MODE_NO_SELFDELETE) {
        swtmr->ucState = OS_SWTMR_STATUS_CREATED;
    } else {
        swtmr->uwOverrun++;
        swtmr->startTime = startTime;
        (VOID)SwtmrToStart(swtmr, ArchCurrCpuid());
    }

    SWTMR_UNLOCK(intSave);
}

STATIC INLINE VOID ScanSwtmrTimeList(SwtmrRunQue *srq)
{
    UINT32 intSave;
    SortLinkAttribute *swtmrSortLink = &srq->swtmrSortLink;
    LOS_DL_LIST *listObject = &swtmrSortLink->sortLink;

    /*
     * it needs to be carefully coped with, since the swtmr is in specific sortlink
     * while other cores still has the chance to process it, like stop the timer.
     */
    LOS_SpinLockSave(&swtmrSortLink->spinLock, &intSave);

    if (LOS_ListEmpty(listObject)) {
        LOS_SpinUnlockRestore(&swtmrSortLink->spinLock, intSave);
        return;
    }
    SortLinkList *sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);

    UINT64 currTime = OsGetCurrSchedTimeCycle();
    while (sortList->responseTime <= currTime) {
        sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
        UINT64 startTime = GET_SORTLIST_VALUE(sortList);
        OsDeleteNodeSortLink(swtmrSortLink, sortList);
        LOS_SpinUnlockRestore(&swtmrSortLink->spinLock, intSave);

        SwtmrWake(srq, startTime, sortList);

        LOS_SpinLockSave(&swtmrSortLink->spinLock, &intSave);
        if (LOS_ListEmpty(listObject)) {
            break;
        }

        sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
    }

    LOS_SpinUnlockRestore(&swtmrSortLink->spinLock, intSave);
    return;
}

STATIC VOID SwtmrTask(VOID)
{
    SwtmrHandlerItem swtmrHandle;
    UINT32 intSave;
    UINT64 waitTime;

    SwtmrRunQue *srq = &g_swtmrRunQue[ArchCurrCpuid()];
    LOS_DL_LIST *head = &srq->swtmrHandlerQueue;
    for (;;) {
        waitTime = OsSortLinkGetNextExpireTime(OsGetCurrSchedTimeCycle(), &srq->swtmrSortLink);
        if (waitTime != 0) {
            SCHEDULER_LOCK(intSave);
            OsSchedDelay(srq->swtmrTask, waitTime);
            OsHookCall(LOS_HOOK_TYPE_MOVEDTASKTODELAYEDLIST, srq->swtmrTask);
            SCHEDULER_UNLOCK(intSave);
        }

        ScanSwtmrTimeList(srq);

        while (!LOS_ListEmpty(head)) {
            SwtmrHandlerItemPtr swtmrHandlePtr = LOS_DL_LIST_ENTRY(LOS_DL_LIST_FIRST(head), SwtmrHandlerItem, node);
            LOS_ListDelete(&swtmrHandlePtr->node);

            (VOID)memcpy_s(&swtmrHandle, sizeof(SwtmrHandlerItem), swtmrHandlePtr, sizeof(SwtmrHandlerItem));
            (VOID)LOS_MemboxFree(g_swtmrHandlerPool, swtmrHandlePtr);
            SwtmrHandler(&swtmrHandle);
        }
    }
}

STATIC UINT32 SwtmrTaskCreate(UINT16 cpuid, UINT32 *swtmrTaskID)
{
    UINT32 ret;
    TSK_INIT_PARAM_S swtmrTask;

    (VOID)memset_s(&swtmrTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    swtmrTask.pfnTaskEntry = (TSK_ENTRY_FUNC)SwtmrTask;
    swtmrTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    swtmrTask.pcName = "Swt_Task";
    swtmrTask.usTaskPrio = 0;
    swtmrTask.uwResved = LOS_TASK_STATUS_DETACHED;
#ifdef LOSCFG_KERNEL_SMP
    swtmrTask.usCpuAffiMask   = CPUID_TO_AFFI_MASK(cpuid);
#endif
    ret = LOS_TaskCreate(swtmrTaskID, &swtmrTask);
    if (ret == LOS_OK) {
        OS_TCB_FROM_TID(*swtmrTaskID)->taskStatus |= OS_TASK_FLAG_SYSTEM_TASK;
    }

    return ret;
}

UINT32 OsSwtmrTaskIDGetByCpuid(UINT16 cpuid)
{
    return g_swtmrRunQue[cpuid].swtmrTask->taskID;
}

BOOL OsIsSwtmrTask(const LosTaskCB *taskCB)
{
    if (taskCB->taskEntry == (TSK_ENTRY_FUNC)SwtmrTask) {
        return TRUE;
    }
    return FALSE;
}

LITE_OS_SEC_TEXT_INIT VOID OsSwtmrRecycle(UINT32 processID)
{
    for (UINT16 index = 0; index < LOSCFG_BASE_CORE_SWTMR_LIMIT; index++) {
        if (g_swtmrCBArray[index].uwOwnerPid == processID) {
            LOS_SwtmrDelete(index);
        }
    }
}

LITE_OS_SEC_TEXT_INIT UINT32 OsSwtmrInit(VOID)
{
    UINT32 size;
    UINT16 index;
    UINT32 ret;
    SWTMR_CTRL_S *swtmr = NULL;
    UINT32 swtmrHandlePoolSize;
    UINT32 cpuid = ArchCurrCpuid();
    UINT32 swtmrTaskID;

    if (cpuid == 0) {
        size = sizeof(SWTMR_CTRL_S) * LOSCFG_BASE_CORE_SWTMR_LIMIT;
        swtmr = (SWTMR_CTRL_S *)LOS_MemAlloc(m_aucSysMem0, size); /* system resident resource */
        if (swtmr == NULL) {
            ret = LOS_ERRNO_SWTMR_NO_MEMORY;
            goto ERROR;
        }

        (VOID)memset_s(swtmr, size, 0, size);
        g_swtmrCBArray = swtmr;
        LOS_ListInit(&g_swtmrFreeList);
        for (index = 0; index < LOSCFG_BASE_CORE_SWTMR_LIMIT; index++, swtmr++) {
            swtmr->usTimerID = index;
            LOS_ListTailInsert(&g_swtmrFreeList, &swtmr->stSortList.sortLinkNode);
        }

        swtmrHandlePoolSize = LOS_MEMBOX_SIZE(sizeof(SwtmrHandlerItem), OS_SWTMR_HANDLE_QUEUE_SIZE);

        g_swtmrHandlerPool = (UINT8 *)LOS_MemAlloc(m_aucSysMem1, swtmrHandlePoolSize); /* system resident resource */
        if (g_swtmrHandlerPool == NULL) {
            ret = LOS_ERRNO_SWTMR_NO_MEMORY;
            goto ERROR;
        }

        ret = LOS_MemboxInit(g_swtmrHandlerPool, swtmrHandlePoolSize, sizeof(SwtmrHandlerItem));
        if (ret != LOS_OK) {
            (VOID)LOS_MemFree(m_aucSysMem1, g_swtmrHandlerPool);
            ret = LOS_ERRNO_SWTMR_HANDLER_POOL_NO_MEM;
            goto ERROR;
        }

        for (UINT16 index = 0; index < LOSCFG_KERNEL_CORE_NUM; index++) {
            SwtmrRunQue *srq = &g_swtmrRunQue[index];
            /* The linked list of all cores must be initialized at core 0 startup for load balancing */
            OsSortLinkInit(&srq->swtmrSortLink);
            LOS_ListInit(&srq->swtmrHandlerQueue);
            srq->swtmrTask = NULL;
        }

        SwtmrDebugDataInit();
    }

    ret = SwtmrTaskCreate(cpuid, &swtmrTaskID);
    if (ret != LOS_OK) {
        ret = LOS_ERRNO_SWTMR_TASK_CREATE_FAILED;
        goto ERROR;
    }

    SwtmrRunQue *srq = &g_swtmrRunQue[cpuid];
    srq->swtmrTask = OsGetTaskCB(swtmrTaskID);
    return LOS_OK;

ERROR:
    PRINT_ERR("OsSwtmrInit error! ret = %u\n", ret);
    return ret;
}

#ifdef LOSCFG_KERNEL_SMP
STATIC INLINE VOID FindIdleSwtmrRunQue(UINT16 *idleCpuid)
{
    SwtmrRunQue *idleRq = &g_swtmrRunQue[0];
    UINT32 nodeNum = OsGetSortLinkNodeNum(&idleRq->swtmrSortLink);
    UINT16 cpuid = 1;
    do {
        SwtmrRunQue *srq = &g_swtmrRunQue[cpuid];
        UINT32 temp = OsGetSortLinkNodeNum(&srq->swtmrSortLink);
        if (nodeNum > temp) {
            *idleCpuid = cpuid;
            nodeNum = temp;
        }
        cpuid++;
    } while (cpuid < LOSCFG_KERNEL_CORE_NUM);
}
#endif

STATIC INLINE VOID AddSwtmr2TimeList(SortLinkList *node, UINT64 responseTime, UINT16 cpuid)
{
    SwtmrRunQue *srq = &g_swtmrRunQue[cpuid];
    OsAdd2SortLink(&srq->swtmrSortLink, node, responseTime, cpuid);
}

STATIC INLINE VOID DeSwtmrFromTimeList(SortLinkList *node)
{
#ifdef LOSCFG_KERNEL_SMP
    UINT16 cpuid = OsGetSortLinkNodeCpuid(node);
#else
    UINT16 cpuid = 0;
#endif
    SwtmrRunQue *srq = &g_swtmrRunQue[cpuid];
    OsDeleteFromSortLink(&srq->swtmrSortLink, node);
    return;
}

STATIC VOID SwtmrAdjustCheck(UINT16 cpuid, UINT64 responseTime)
{
    UINT32 ret;
    UINT32 intSave;
    SwtmrRunQue *srq = &g_swtmrRunQue[cpuid];
    SCHEDULER_LOCK(intSave);
    if ((srq->swtmrTask == NULL) || !OsTaskIsBlocked(srq->swtmrTask)) {
        SCHEDULER_UNLOCK(intSave);
        return;
    }

    if (responseTime >= GET_SORTLIST_VALUE(&srq->swtmrTask->sortList)) {
        SCHEDULER_UNLOCK(intSave);
        return;
    }

    ret = OsSchedAdjustTaskFromTimeList(srq->swtmrTask, responseTime);
    SCHEDULER_UNLOCK(intSave);
    if (ret != LOS_OK) {
        return;
    }

    if (cpuid == ArchCurrCpuid()) {
        OsSchedUpdateExpireTime();
    } else {
        LOS_MpSchedule(CPUID_TO_AFFI_MASK(cpuid));
    }
}

STATIC UINT64 SwtmrToStart(SWTMR_CTRL_S *swtmr, UINT16 cpuid)
{
    UINT32 ticks;
    UINT32 times = 0;

    if ((swtmr->uwOverrun == 0) && ((swtmr->ucMode == LOS_SWTMR_MODE_ONCE) ||
        (swtmr->ucMode == LOS_SWTMR_MODE_OPP) ||
        (swtmr->ucMode == LOS_SWTMR_MODE_NO_SELFDELETE))) {
        ticks = swtmr->uwExpiry;
    } else {
        ticks = swtmr->uwInterval;
    }
    swtmr->ucState = OS_SWTMR_STATUS_TICKING;

    UINT64 period = (UINT64)ticks * OS_CYCLE_PER_TICK;
    UINT64 responseTime = swtmr->startTime + period;
    UINT64 currTime = OsGetCurrSchedTimeCycle();
    if (responseTime < currTime) {
        times = (UINT32)((currTime - swtmr->startTime) / period);
        swtmr->startTime += times * period;
        responseTime = swtmr->startTime + period;
        PRINT_WARN("Swtmr already timeout! SwtmrID: %u\n", swtmr->usTimerID);
    }

    AddSwtmr2TimeList(&swtmr->stSortList, responseTime, cpuid);
    SwtmrDebugDataUpdate(swtmr, ticks, times);
    return responseTime;
}

/*
 * Description: Start Software Timer
 * Input      : swtmr --- Need to start software timer
 */
STATIC INLINE VOID SwtmrStart(SWTMR_CTRL_S *swtmr)
{
    UINT64 responseTime;
    UINT16 idleCpu = 0;
#ifdef LOSCFG_KERNEL_SMP
    FindIdleSwtmrRunQue(&idleCpu);
#endif
    swtmr->startTime = OsGetCurrSchedTimeCycle();
    responseTime = SwtmrToStart(swtmr, idleCpu);

    SwtmrDebugDataStart(swtmr, idleCpu);

    SwtmrAdjustCheck(idleCpu, responseTime);
}

/*
 * Description: Delete Software Timer
 * Input      : swtmr --- Need to delete software timer, When using, Ensure that it can't be NULL.
 */
STATIC INLINE VOID SwtmrDelete(SWTMR_CTRL_S *swtmr)
{
    /* insert to free list */
    LOS_ListTailInsert(&g_swtmrFreeList, &swtmr->stSortList.sortLinkNode);
    swtmr->ucState = OS_SWTMR_STATUS_UNUSED;
    swtmr->uwOwnerPid = 0;

    SwtmrDebugDataClear(swtmr->usTimerID);
}

STATIC INLINE VOID SwtmrRestart(UINT64 startTime, SortLinkList *sortList, UINT16 cpuid)
{
    UINT32 intSave;

    SWTMR_CTRL_S *swtmr = LOS_DL_LIST_ENTRY(sortList, SWTMR_CTRL_S, stSortList);
    SWTMR_LOCK(intSave);
    swtmr->startTime = startTime;
    (VOID)SwtmrToStart(swtmr, cpuid);
    SWTMR_UNLOCK(intSave);
}

VOID OsSwtmrResponseTimeReset(UINT64 startTime)
{
    UINT16 cpuid = ArchCurrCpuid();
    SortLinkAttribute *swtmrSortLink = &g_swtmrRunQue[cpuid].swtmrSortLink;
    LOS_DL_LIST *listHead = &swtmrSortLink->sortLink;
    LOS_DL_LIST *listNext = listHead->pstNext;

    LOS_SpinLock(&swtmrSortLink->spinLock);
    while (listNext != listHead) {
        SortLinkList *sortList = LOS_DL_LIST_ENTRY(listNext, SortLinkList, sortLinkNode);
        OsDeleteNodeSortLink(swtmrSortLink, sortList);
        LOS_SpinUnlock(&swtmrSortLink->spinLock);

        SwtmrRestart(startTime, sortList, cpuid);

        LOS_SpinLock(&swtmrSortLink->spinLock);
        listNext = listNext->pstNext;
    }
    LOS_SpinUnlock(&swtmrSortLink->spinLock);
}

STATIC INLINE BOOL SwtmrRunQueFind(SortLinkAttribute *swtmrSortLink, SCHED_TL_FIND_FUNC checkFunc, UINTPTR arg)
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

STATIC BOOL SwtmrTimeListFind(SCHED_TL_FIND_FUNC checkFunc, UINTPTR arg)
{
    for (UINT16 cpuid = 0; cpuid < LOSCFG_KERNEL_CORE_NUM; cpuid++) {
        SortLinkAttribute *swtmrSortLink = &g_swtmrRunQue[ArchCurrCpuid()].swtmrSortLink;
        if (SwtmrRunQueFind(swtmrSortLink, checkFunc, arg)) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL OsSwtmrWorkQueueFind(SCHED_TL_FIND_FUNC checkFunc, UINTPTR arg)
{
    UINT32 intSave;

    SWTMR_LOCK(intSave);
    BOOL find = SwtmrTimeListFind(checkFunc, arg);
    SWTMR_UNLOCK(intSave);
    return find;
}

/*
 * Description: Get next timeout
 * Return     : Count of the Timer list
 */
LITE_OS_SEC_TEXT UINT32 OsSwtmrGetNextTimeout(VOID)
{
    UINT64 currTime = OsGetCurrSchedTimeCycle();
    SwtmrRunQue *srq = &g_swtmrRunQue[ArchCurrCpuid()];
    UINT64 time = (OsSortLinkGetNextExpireTime(currTime, &srq->swtmrSortLink) / OS_CYCLE_PER_TICK);
    if (time > OS_INVALID_VALUE) {
        time = OS_INVALID_VALUE;
    }
    return (UINT32)time;
}

/*
 * Description: Stop of Software Timer interface
 * Input      : swtmr --- the software timer control handler
 */
STATIC VOID SwtmrStop(SWTMR_CTRL_S *swtmr)
{
    swtmr->ucState = OS_SWTMR_STATUS_CREATED;
    swtmr->uwOverrun = 0;

    DeSwtmrFromTimeList(&swtmr->stSortList);
}

/*
 * Description: Get next software timer expiretime
 * Input      : swtmr --- the software timer control handler
 */
LITE_OS_SEC_TEXT STATIC UINT32 OsSwtmrTimeGet(const SWTMR_CTRL_S *swtmr)
{
    UINT64 currTime = OsGetCurrSchedTimeCycle();
    UINT64 time = (OsSortLinkGetTargetExpireTime(currTime, &swtmr->stSortList) / OS_CYCLE_PER_TICK);
    if (time > OS_INVALID_VALUE) {
        time = OS_INVALID_VALUE;
    }
    return (UINT32)time;
}

LITE_OS_SEC_TEXT_INIT UINT32 LOS_SwtmrCreate(UINT32 interval,
                                             UINT8 mode,
                                             SWTMR_PROC_FUNC handler,
                                             UINT16 *swtmrID,
                                             UINTPTR arg)
{
    SWTMR_CTRL_S *swtmr = NULL;
    UINT32 intSave;
    SortLinkList *sortList = NULL;

    if (interval == 0) {
        return LOS_ERRNO_SWTMR_INTERVAL_NOT_SUITED;
    }

    if ((mode != LOS_SWTMR_MODE_ONCE) && (mode != LOS_SWTMR_MODE_PERIOD) &&
        (mode != LOS_SWTMR_MODE_NO_SELFDELETE)) {
        return LOS_ERRNO_SWTMR_MODE_INVALID;
    }

    if (handler == NULL) {
        return LOS_ERRNO_SWTMR_PTR_NULL;
    }

    if (swtmrID == NULL) {
        return LOS_ERRNO_SWTMR_RET_PTR_NULL;
    }

    SWTMR_LOCK(intSave);
    if (LOS_ListEmpty(&g_swtmrFreeList)) {
        SWTMR_UNLOCK(intSave);
        return LOS_ERRNO_SWTMR_MAXSIZE;
    }

    sortList = LOS_DL_LIST_ENTRY(g_swtmrFreeList.pstNext, SortLinkList, sortLinkNode);
    swtmr = LOS_DL_LIST_ENTRY(sortList, SWTMR_CTRL_S, stSortList);
    LOS_ListDelete(LOS_DL_LIST_FIRST(&g_swtmrFreeList));
    SWTMR_UNLOCK(intSave);

    swtmr->uwOwnerPid = OsCurrProcessGet()->processID;
    swtmr->pfnHandler = handler;
    swtmr->ucMode = mode;
    swtmr->uwOverrun = 0;
    swtmr->uwInterval = interval;
    swtmr->uwExpiry = interval;
    swtmr->uwArg = arg;
    swtmr->ucState = OS_SWTMR_STATUS_CREATED;
    SET_SORTLIST_VALUE(&swtmr->stSortList, OS_SORT_LINK_INVALID_TIME);
    *swtmrID = swtmr->usTimerID;
    OsHookCall(LOS_HOOK_TYPE_SWTMR_CREATE, swtmr);
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 LOS_SwtmrStart(UINT16 swtmrID)
{
    SWTMR_CTRL_S *swtmr = NULL;
    UINT32 intSave;
    UINT32 ret = LOS_OK;
    UINT16 swtmrCBID;

    if (swtmrID >= OS_SWTMR_MAX_TIMERID) {
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }

    swtmrCBID = swtmrID % LOSCFG_BASE_CORE_SWTMR_LIMIT;
    swtmr = g_swtmrCBArray + swtmrCBID;

    SWTMR_LOCK(intSave);
    if (swtmr->usTimerID != swtmrID) {
        SWTMR_UNLOCK(intSave);
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }

    switch (swtmr->ucState) {
        case OS_SWTMR_STATUS_UNUSED:
            ret = LOS_ERRNO_SWTMR_NOT_CREATED;
            break;
        /*
         * If the status of swtmr is timing, it should stop the swtmr first,
         * then start the swtmr again.
         */
        case OS_SWTMR_STATUS_TICKING:
            SwtmrStop(swtmr);
            /* fall-through */
        case OS_SWTMR_STATUS_CREATED:
            SwtmrStart(swtmr);
            break;
        default:
            ret = LOS_ERRNO_SWTMR_STATUS_INVALID;
            break;
    }

    SWTMR_UNLOCK(intSave);
    OsHookCall(LOS_HOOK_TYPE_SWTMR_START, swtmr);
    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_SwtmrStop(UINT16 swtmrID)
{
    SWTMR_CTRL_S *swtmr = NULL;
    UINT32 intSave;
    UINT32 ret = LOS_OK;
    UINT16 swtmrCBID;

    if (swtmrID >= OS_SWTMR_MAX_TIMERID) {
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }

    swtmrCBID = swtmrID % LOSCFG_BASE_CORE_SWTMR_LIMIT;
    swtmr = g_swtmrCBArray + swtmrCBID;
    SWTMR_LOCK(intSave);

    if (swtmr->usTimerID != swtmrID) {
        SWTMR_UNLOCK(intSave);
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }

    switch (swtmr->ucState) {
        case OS_SWTMR_STATUS_UNUSED:
            ret = LOS_ERRNO_SWTMR_NOT_CREATED;
            break;
        case OS_SWTMR_STATUS_CREATED:
            ret = LOS_ERRNO_SWTMR_NOT_STARTED;
            break;
        case OS_SWTMR_STATUS_TICKING:
            SwtmrStop(swtmr);
            break;
        default:
            ret = LOS_ERRNO_SWTMR_STATUS_INVALID;
            break;
    }

    SWTMR_UNLOCK(intSave);
    OsHookCall(LOS_HOOK_TYPE_SWTMR_STOP, swtmr);
    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_SwtmrTimeGet(UINT16 swtmrID, UINT32 *tick)
{
    SWTMR_CTRL_S *swtmr = NULL;
    UINT32 intSave;
    UINT32 ret = LOS_OK;
    UINT16 swtmrCBID;

    if (swtmrID >= OS_SWTMR_MAX_TIMERID) {
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }

    if (tick == NULL) {
        return LOS_ERRNO_SWTMR_TICK_PTR_NULL;
    }

    swtmrCBID = swtmrID % LOSCFG_BASE_CORE_SWTMR_LIMIT;
    swtmr = g_swtmrCBArray + swtmrCBID;
    SWTMR_LOCK(intSave);

    if (swtmr->usTimerID != swtmrID) {
        SWTMR_UNLOCK(intSave);
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }
    switch (swtmr->ucState) {
        case OS_SWTMR_STATUS_UNUSED:
            ret = LOS_ERRNO_SWTMR_NOT_CREATED;
            break;
        case OS_SWTMR_STATUS_CREATED:
            ret = LOS_ERRNO_SWTMR_NOT_STARTED;
            break;
        case OS_SWTMR_STATUS_TICKING:
            *tick = OsSwtmrTimeGet(swtmr);
            break;
        default:
            ret = LOS_ERRNO_SWTMR_STATUS_INVALID;
            break;
    }
    SWTMR_UNLOCK(intSave);
    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_SwtmrDelete(UINT16 swtmrID)
{
    SWTMR_CTRL_S *swtmr = NULL;
    UINT32 intSave;
    UINT32 ret = LOS_OK;
    UINT16 swtmrCBID;

    if (swtmrID >= OS_SWTMR_MAX_TIMERID) {
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }

    swtmrCBID = swtmrID % LOSCFG_BASE_CORE_SWTMR_LIMIT;
    swtmr = g_swtmrCBArray + swtmrCBID;
    SWTMR_LOCK(intSave);

    if (swtmr->usTimerID != swtmrID) {
        SWTMR_UNLOCK(intSave);
        return LOS_ERRNO_SWTMR_ID_INVALID;
    }

    switch (swtmr->ucState) {
        case OS_SWTMR_STATUS_UNUSED:
            ret = LOS_ERRNO_SWTMR_NOT_CREATED;
            break;
        case OS_SWTMR_STATUS_TICKING:
            SwtmrStop(swtmr);
            /* fall-through */
        case OS_SWTMR_STATUS_CREATED:
            SwtmrDelete(swtmr);
            break;
        default:
            ret = LOS_ERRNO_SWTMR_STATUS_INVALID;
            break;
    }

    SWTMR_UNLOCK(intSave);
    OsHookCall(LOS_HOOK_TYPE_SWTMR_DELETE, swtmr);
    return ret;
}

#endif /* LOSCFG_BASE_CORE_SWTMR_ENABLE */
