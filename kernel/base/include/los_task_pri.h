/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2022 Huawei Device Co., Ltd. All rights reserved.
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

#ifndef _LOS_TASK_PRI_H
#define _LOS_TASK_PRI_H

#include "los_task.h"
#include "los_sched_pri.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/**
 * @ingroup los_task
 * Define task signal types.
 *
 * Task signal types.
 */
#define SIGNAL_NONE                 0U
#define SIGNAL_KILL                 (1U << 0)
#define SIGNAL_SUSPEND              (1U << 1)
#define SIGNAL_AFFI                 (1U << 2)

/* scheduler lock */
extern SPIN_LOCK_S g_taskSpin;
#define SCHEDULER_LOCK(state)       LOS_SpinLockSave(&g_taskSpin, &(state))
#define SCHEDULER_UNLOCK(state)     LOS_SpinUnlockRestore(&g_taskSpin, state)

/* default and non-running task's ownership id */
#define OS_TASK_INVALID_CPUID       0xFFFF

/**
 * @ingroup los_task
 * Null task ID
 *
 */
#define OS_TASK_ERRORID             0xFFFFFFFF

/**
 * @ingroup los_task
 * Flag that indicates the task or task control block status.
 *
 * The task control block is unused.
 */
#define OS_TASK_STATUS_UNUSED       0x0400U

/**
 * @ingroup los_task
 * Flag that indicates the task or task control block status.
 *
 * The task is joinable.
 */
#define OS_TASK_FLAG_PTHREAD_JOIN   0x0800U

/**
 * @ingroup los_task
 * Flag that indicates the task or task control block status.
 *
 * The task is user mode task.
 */
#define OS_TASK_FLAG_USER_MODE      0x1000U

/**
 * @ingroup los_task
 * Flag that indicates the task property.
 *
 * The task is system-level task, like idle, swtmr and etc.
 */
#define OS_TASK_FLAG_SYSTEM_TASK    0x2000U

/**
 * @ingroup los_task
 * Flag that indicates the task property.
 *
 * The task is no-delete system task, like resourceTask.
 */
#define OS_TASK_FLAG_NO_DELETE      0x4000U

/**
 * @ingroup los_task
 * Flag that indicates the task property.
 *
 * Kills the thread during process exit.
 */
#define OS_TASK_FLAG_EXIT_KILL       0x8000U

/**
 * @ingroup los_task
 * Flag that indicates the task property.
 *
 * Specifies the process creation task.
 */
#define OS_TASK_FLAG_SPECIFIES_PROCESS 0x0U

/**
 * @ingroup los_task
 * Boundary on which the stack size is aligned.
 *
 */
#define OS_TASK_STACK_SIZE_ALIGN    16U

/**
 * @ingroup los_task
 * Boundary on which the stack address is aligned.
 *
 */
#define OS_TASK_STACK_ADDR_ALIGN    8U

/**
 * @ingroup los_task
 * Number of usable task priorities.
 */
#define OS_TSK_PRINUM               (OS_TASK_PRIORITY_LOWEST - OS_TASK_PRIORITY_HIGHEST + 1)

/**
* @ingroup  los_task
* @brief Check whether a task ID is valid.
*
* @par Description:
* This API is used to check whether a task ID, excluding the idle task ID, is valid.
* @attention None.
*
* @param  taskID [IN] Task ID.
*
* @retval 0 or 1. One indicates that the task ID is invalid, whereas zero indicates that the task ID is valid.
* @par Dependency:
* <ul><li>los_task_pri.h: the header file that contains the API declaration.</li></ul>
* @see
*/
#define OS_TSK_GET_INDEX(taskID) (taskID)

/**
* @ingroup  los_task
* @brief Obtain the pointer to a task control block.
*
* @par Description:
* This API is used to obtain the pointer to a task control block using a corresponding parameter.
* @attention None.
*
* @param  ptr [IN] Parameter used for obtaining the task control block.
*
* @retval Pointer to the task control block.
* @par Dependency:
* <ul><li>los_task_pri.h: the header file that contains the API declaration.</li></ul>
* @see
*/
#define OS_TCB_FROM_PENDLIST(ptr) LOS_DL_LIST_ENTRY(ptr, LosTaskCB, pendList)

/**
* @ingroup  los_task
* @brief Obtain the pointer to a task control block.
*
* @par Description:
* This API is used to obtain the pointer to a task control block that has a specified task ID.
* @attention None.
*
* @param  TaskID [IN] Task ID.
*
* @retval Pointer to the task control block.
* @par Dependency:
* <ul><li>los_task_pri.h: the header file that contains the API declaration.</li></ul>
* @see
*/
#define OS_TCB_FROM_TID(taskID) (((LosTaskCB *)g_taskCBArray) + (taskID))

#ifndef LOSCFG_STACK_POINT_ALIGN_SIZE
#define LOSCFG_STACK_POINT_ALIGN_SIZE                       (sizeof(UINTPTR) * 2)
#endif

#define OS_TASK_RESOURCE_STATIC_SIZE    0x1000
#define OS_TASK_RESOURCE_FREE_PRIORITY  5
#define OS_RESOURCE_EVENT_MASK          0xFF
#define OS_RESOURCE_EVENT_OOM           0x02
#define OS_RESOURCE_EVENT_FREE          0x04

typedef struct {
    LosTaskCB *runTask;
    LosTaskCB *newTask;
} LosTask;

struct ProcessSignalInfo {
    siginfo_t *sigInfo;       /**< Signal to be dispatched */
    LosTaskCB *defaultTcb;    /**< Default TCB */
    LosTaskCB *unblockedTcb;  /**< The signal unblock on this TCB*/
    LosTaskCB *awakenedTcb;   /**< This TCB was awakened */
    LosTaskCB *receivedTcb;   /**< This TCB received the signal */
};

typedef int (*ForEachTaskCB)(LosTaskCB *tcb, void *arg);

/**
 * @ingroup los_task
 * Maximum number of tasks.
 *
 */
extern UINT32 g_taskMaxNum;

/**
 * @ingroup los_task
 * Starting address of a task.
 *
 */
extern LosTaskCB *g_taskCBArray;

/**
 * @ingroup los_task
 * Time slice structure.
 */
typedef struct {
    LosTaskCB *task; /**< Current running task */
    UINT16 time;     /**< Expiration time point */
    UINT16 timeout;  /**< Expiration duration */
} OsTaskRobin;

STATIC INLINE LosTaskCB *OsGetTaskCB(UINT32 taskID)
{
    return OS_TCB_FROM_TID(taskID);
}

STATIC INLINE BOOL OsTaskIsUnused(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & OS_TASK_STATUS_UNUSED) != 0);
}

STATIC INLINE BOOL OsTaskIsKilled(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & OS_TASK_FLAG_EXIT_KILL) != 0);
}

STATIC INLINE BOOL OsTaskIsUserMode(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & OS_TASK_FLAG_USER_MODE) != 0);
}

#define OS_TID_CHECK_INVALID(taskID) ((UINT32)(taskID) >= g_taskMaxNum)

/* get task info */
#define OS_ALL_TASK_MASK  0xFFFFFFFF

#define OS_TASK_WAIT_ANYPROCESS (1 << 0U)
#define OS_TASK_WAIT_PROCESS    (1 << 1U)
#define OS_TASK_WAIT_GID        (1 << 2U)
#define OS_TASK_WAIT_SEM        (OS_TASK_WAIT_GID + 1)
#define OS_TASK_WAIT_QUEUE      (OS_TASK_WAIT_SEM + 1)
#define OS_TASK_WAIT_JOIN       (OS_TASK_WAIT_QUEUE + 1)
#define OS_TASK_WAIT_SIGNAL     (OS_TASK_WAIT_JOIN + 1)
#define OS_TASK_WAIT_LITEIPC    (OS_TASK_WAIT_SIGNAL + 1)
#define OS_TASK_WAIT_MUTEX      (OS_TASK_WAIT_LITEIPC + 1)
#define OS_TASK_WAIT_FUTEX      (OS_TASK_WAIT_MUTEX + 1)
#define OS_TASK_WAIT_EVENT      (OS_TASK_WAIT_FUTEX + 1)
#define OS_TASK_WAIT_COMPLETE   (OS_TASK_WAIT_EVENT + 1)

STATIC INLINE VOID OsTaskWaitSetPendMask(UINT16 mask, UINTPTR lockID, UINT32 timeout)
{
    LosTaskCB *runTask = OsCurrTaskGet();
    runTask->waitID = lockID;
    runTask->waitFlag = mask;
    (VOID)timeout;
}

STATIC INLINE VOID OsTaskWakeClearPendMask(LosTaskCB *resumeTask)
{
    resumeTask->waitID = 0;
    resumeTask->waitFlag = 0;
}

extern UINT32 OsTaskSetDetachUnsafe(LosTaskCB *taskCB);
extern VOID OsTaskJoinPostUnsafe(LosTaskCB *taskCB);
extern UINT32 OsTaskJoinPendUnsafe(LosTaskCB *taskCB);
extern BOOL OsTaskCpuAffiSetUnsafe(UINT32 taskID, UINT16 newCpuAffiMask, UINT16 *oldCpuAffiMask);
extern VOID OsTaskSchedule(LosTaskCB *, LosTaskCB *);
extern VOID OsTaskContextLoad(LosTaskCB *newTask);
extern VOID OsIdleTask(VOID);
extern UINT32 OsIdleTaskCreate(VOID);
extern UINT32 OsTaskInit(VOID);
extern UINT32 OsShellCmdDumpTask(INT32 argc, const CHAR **argv);
extern UINT32 OsShellCmdTskInfoGet(UINT32 taskID, VOID *seqfile, UINT16 flag);
extern LosTaskCB *OsGetMainTask(VOID);
extern VOID OsSetMainTask(VOID);
extern UINT32 OsGetIdleTaskId(VOID);
extern VOID OsTaskEntry(UINT32 taskID);
extern VOID OsTaskProcSignal(VOID);
extern UINT32 OsCreateUserTask(UINT32 processID, TSK_INIT_PARAM_S *initParam);
extern INT32 OsSetTaskName(LosTaskCB *taskCB, const CHAR *name, BOOL setPName);
extern VOID OsTaskCBRecycleToFree(VOID);
extern VOID OsRunningTaskToExit(LosTaskCB *runTask, UINT32 status);
extern INT32 OsUserTaskOperatePermissionsCheck(const LosTaskCB *taskCB);
extern INT32 OsUserProcessOperatePermissionsCheck(const LosTaskCB *taskCB, UINT32 processID);
extern INT32 OsTcbDispatch(LosTaskCB *stcb, siginfo_t *info);
extern VOID OsWriteResourceEvent(UINT32 events);
extern VOID OsWriteResourceEventUnsafe(UINT32 events);
extern UINT32 OsResourceFreeTaskCreate(VOID);
extern VOID OsTaskInsertToRecycleList(LosTaskCB *taskCB);
extern VOID OsInactiveTaskDelete(LosTaskCB *taskCB);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_TASK_PRI_H */
