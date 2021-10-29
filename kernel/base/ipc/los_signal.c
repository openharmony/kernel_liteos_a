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

#include "los_signal.h"
#include "pthread.h"
#include "los_process_pri.h"
#include "los_sched_pri.h"
#include "los_hw_pri.h"
#include "user_copy.h"
#ifdef LOSCFG_SECURITY_CAPABILITY
#include "capability_api.h"
#endif
#include "los_atomic.h"

#ifdef LOSCFG_KERNEL_VM

int raise(int sig)
{
    (VOID)sig;
    PRINT_ERR("%s NOT SUPPORT\n", __FUNCTION__);
    errno = ENOSYS;
    return -1;
}

#define GETUNMASKSET(procmask, pendFlag) ((~(procmask)) & (sigset_t)(pendFlag))
#define UINT64_BIT_SIZE 64

int OsSigIsMember(const sigset_t *set, int signo)
{
    int ret = LOS_NOK;
    /* In musl, sig No bits 00000100 present sig No 3, but  1<< 3 = 00001000, so signo needs minus 1 */
    signo -= 1;
    /* Verify the signal */
    if (GOOD_SIGNO(signo)) {
        /* Check if the signal is in the set */
        ret = ((*set & SIGNO2SET((unsigned int)signo)) != 0);
    }

    return ret;
}

STATIC VOID OsMoveTmpInfoToUnbInfo(sig_cb *sigcb, INT32 signo)
{
    SigInfoListNode *tmpInfoNode = sigcb->tmpInfoListHead;
    SigInfoListNode **prevHook = &sigcb->tmpInfoListHead;
    while (tmpInfoNode != NULL) {
        if (tmpInfoNode->info.si_signo == signo) {
            /* copy tmpinfo to unbinfo. */
            (VOID)memcpy_s(&sigcb->sigunbinfo, sizeof(siginfo_t), &tmpInfoNode->info, sizeof(siginfo_t));
            /* delete tmpinfo from tmpList. */
            *prevHook = tmpInfoNode->next;
            (VOID)LOS_MemFree(m_aucSysMem0, tmpInfoNode);
            tmpInfoNode = *prevHook;
            break;
        }
        prevHook = &tmpInfoNode->next;
        tmpInfoNode = tmpInfoNode->next;
    }
}

STATIC INT32 OsAddSigInfoToTmpList(sig_cb *sigcb, siginfo_t *info)
{
    /* try to find the old siginfo */
    SigInfoListNode *tmp = sigcb->tmpInfoListHead;
    while (tmp != NULL) {
        if (tmp->info.si_signo == info->si_signo) {
            /* found it, break. */
            break;
        }
        tmp = tmp->next;
    }

    if (tmp == NULL) {
        /* none, alloc new one */
        tmp = (SigInfoListNode *)LOS_MemAlloc(m_aucSysMem0, sizeof(SigInfoListNode));
        if (tmp == NULL) {
            return LOS_NOK;
        }
        tmp->next = sigcb->tmpInfoListHead;
        sigcb->tmpInfoListHead = tmp;
    }

    (VOID)memcpy_s(&tmp->info, sizeof(siginfo_t), info, sizeof(siginfo_t));

    return LOS_OK;
}

VOID OsClearSigInfoTmpList(sig_cb *sigcb)
{
    while (sigcb->tmpInfoListHead != NULL) {
        SigInfoListNode *tmpInfoNode = sigcb->tmpInfoListHead;
        sigcb->tmpInfoListHead = sigcb->tmpInfoListHead->next;
        (VOID)LOS_MemFree(m_aucSysMem0, tmpInfoNode);
    }
}

STATIC INLINE VOID OsSigWaitTaskWake(LosTaskCB *taskCB, INT32 signo)
{
    sig_cb *sigcb = &taskCB->sig;

    if (!LOS_ListEmpty(&sigcb->waitList) && OsSigIsMember(&sigcb->sigwaitmask, signo)) {
        OsMoveTmpInfoToUnbInfo(sigcb, signo);
        OsTaskWakeClearPendMask(taskCB);
        OsSchedTaskWake(taskCB);
        OsSigEmptySet(&sigcb->sigwaitmask);
    }
}

STATIC UINT32 OsPendingTaskWake(LosTaskCB *taskCB, INT32 signo)
{
    if (!OsTaskIsPending(taskCB) || !OsProcessIsUserMode(OS_PCB_FROM_PID(taskCB->processID))) {
        return 0;
    }

    if ((signo != SIGKILL) && (taskCB->waitFlag != OS_TASK_WAIT_SIGNAL)) {
        return 0;
    }

    switch (taskCB->waitFlag) {
        case OS_TASK_WAIT_PROCESS:
        case OS_TASK_WAIT_GID:
        case OS_TASK_WAIT_ANYPROCESS:
            OsWaitWakeTask(taskCB, OS_INVALID_VALUE);
            break;
        case OS_TASK_WAIT_JOIN:
            OsTaskWakeClearPendMask(taskCB);
            OsSchedTaskWake(taskCB);
            break;
        case OS_TASK_WAIT_SIGNAL:
            OsSigWaitTaskWake(taskCB, signo);
            break;
        case OS_TASK_WAIT_LITEIPC:
            OsTaskWakeClearPendMask(taskCB);
            OsSchedTaskWake(taskCB);
            break;
        case OS_TASK_WAIT_FUTEX:
            OsFutexNodeDeleteFromFutexHash(&taskCB->futex, TRUE, NULL, NULL);
            OsTaskWakeClearPendMask(taskCB);
            OsSchedTaskWake(taskCB);
            break;
        default:
            break;
    }

    return 0;
}

int OsTcbDispatch(LosTaskCB *stcb, siginfo_t *info)
{
    bool masked = FALSE;
    sig_cb *sigcb = &stcb->sig;

    OS_RETURN_IF_NULL(sigcb);
    /* If signo is 0, not send signal, just check process or pthread exist */
    if (info->si_signo == 0) {
        return 0;
    }
    masked = (bool)OsSigIsMember(&sigcb->sigprocmask, info->si_signo);
    if (masked) {
        /* If signal is in wait list and mask list, need unblock it */
        if (LOS_ListEmpty(&sigcb->waitList)  ||
            (!LOS_ListEmpty(&sigcb->waitList) && !OsSigIsMember(&sigcb->sigwaitmask, info->si_signo))) {
            OsSigAddSet(&sigcb->sigPendFlag, info->si_signo);
        }
    } else {
        /* unmasked signal actions */
        OsSigAddSet(&sigcb->sigFlag, info->si_signo);
    }

    if (OsAddSigInfoToTmpList(sigcb, info) == LOS_NOK) {
        return -ENOMEM;
    }

    return OsPendingTaskWake(stcb, info->si_signo);
}

void OsSigMaskSwitch(LosTaskCB * const rtcb, sigset_t set)
{
    sigset_t unmaskset;

    rtcb->sig.sigprocmask = set;
    unmaskset = GETUNMASKSET(rtcb->sig.sigprocmask, rtcb->sig.sigPendFlag);
    if (unmaskset != NULL_SIGNAL_SET) {
        /* pendlist do */
        rtcb->sig.sigFlag |= unmaskset;
        rtcb->sig.sigPendFlag ^= unmaskset;
    }
}

int OsSigprocMask(int how, const sigset_t_l *setl, sigset_t_l *oldset)
{
    LosTaskCB *spcb = NULL;
    sigset_t oldSigprocmask;
    int ret = LOS_OK;
    unsigned int intSave;
    sigset_t set;
    int retVal;

    if (setl != NULL) {
        retVal = LOS_ArchCopyFromUser(&set, &(setl->sig[0]), sizeof(sigset_t));
        if (retVal != 0) {
            return -EFAULT;
        }
    }
    SCHEDULER_LOCK(intSave);
    spcb = OsCurrTaskGet();
    /* If requested, copy the old mask to user. */
    oldSigprocmask = spcb->sig.sigprocmask;

    /* If requested, modify the current signal mask. */
    if (setl != NULL) {
        /* Okay, determine what we are supposed to do */
        switch (how) {
            /* Set the union of the current set and the signal
             * set pointed to by set as the new sigprocmask.
             */
            case SIG_BLOCK:
                spcb->sig.sigprocmask |= set;
                break;
            /* Set the intersection of the current set and the
             * signal set pointed to by set as the new sigprocmask.
             */
            case SIG_UNBLOCK:
                spcb->sig.sigprocmask &= ~(set);
                break;
            /* Set the signal set pointed to by set as the new sigprocmask. */
            case SIG_SETMASK:
                spcb->sig.sigprocmask = set;
                break;
            default:
                ret = -EINVAL;
                break;
        }
        /* If pending mask not in sigmask, need set sigflag. */
        OsSigMaskSwitch(spcb, spcb->sig.sigprocmask);
    }
    SCHEDULER_UNLOCK(intSave);

    if (oldset != NULL) {
        retVal = LOS_ArchCopyToUser(&(oldset->sig[0]), &oldSigprocmask, sizeof(sigset_t));
        if (retVal != 0) {
            return -EFAULT;
        }
    }
    return ret;
}

int OsSigProcessForeachChild(LosProcessCB *spcb, ForEachTaskCB handler, void *arg)
{
    int ret;

    /* Visit the main thread last (if present) */
    LosTaskCB *taskCB = NULL;
    LOS_DL_LIST_FOR_EACH_ENTRY(taskCB, &(spcb->threadSiblingList), LosTaskCB, threadList) {
        ret = handler(taskCB, arg);
        OS_RETURN_IF(ret != 0, ret);
    }
    return LOS_OK;
}

static int SigProcessSignalHandler(LosTaskCB *tcb, void *arg)
{
    struct ProcessSignalInfo *info = (struct ProcessSignalInfo *)arg;
    int ret;
    int isMember;

    if (tcb == NULL) {
        return 0;
    }

    /* If the default tcb is not setted, then set this one as default. */
    if (!info->defaultTcb) {
        info->defaultTcb = tcb;
    }

    isMember = OsSigIsMember(&tcb->sig.sigwaitmask, info->sigInfo->si_signo);
    if (isMember && (!info->awakenedTcb)) {
        /* This means the task is waiting for this signal. Stop looking for it and use this tcb.
         * The requirement is: if more than one task in this task group is waiting for the signal,
         * then only one indeterminate task in the group will receive the signal.
         */
        ret = OsTcbDispatch(tcb, info->sigInfo);
        OS_RETURN_IF(ret < 0, ret);

        /* set this tcb as awakenedTcb */
        info->awakenedTcb = tcb;
        OS_RETURN_IF(info->receivedTcb != NULL, SIG_STOP_VISIT); /* Stop search */
    }
    /* Is this signal unblocked on this thread? */
    isMember = OsSigIsMember(&tcb->sig.sigprocmask, info->sigInfo->si_signo);
    if ((!isMember) && (!info->receivedTcb) && (tcb != info->awakenedTcb)) {
        /* if unblockedTcb of this signal is not setted, then set it. */
        if (!info->unblockedTcb) {
            info->unblockedTcb = tcb;
        }

        ret = OsTcbDispatch(tcb, info->sigInfo);
        OS_RETURN_IF(ret < 0, ret);
        /* set this tcb as receivedTcb */
        info->receivedTcb = tcb;
        OS_RETURN_IF(info->awakenedTcb != NULL, SIG_STOP_VISIT); /* Stop search */
    }
    return 0; /* Keep searching */
}

static int SigProcessKillSigHandler(LosTaskCB *tcb, void *arg)
{
    struct ProcessSignalInfo *info = (struct ProcessSignalInfo *)arg;

    return OsPendingTaskWake(tcb, info->sigInfo->si_signo);
}

static void SigProcessLoadTcb(struct ProcessSignalInfo *info, siginfo_t *sigInfo)
{
    LosTaskCB *tcb = NULL;

    if (info->awakenedTcb == NULL && info->receivedTcb == NULL) {
        if (info->unblockedTcb) {
            tcb = info->unblockedTcb;
        } else if (info->defaultTcb) {
            tcb = info->defaultTcb;
        } else {
            return;
        }
        /* Deliver the signal to the selected task */
        (void)OsTcbDispatch(tcb, sigInfo);
    }
}

int OsSigProcessSend(LosProcessCB *spcb, siginfo_t *sigInfo)
{
    int ret;
    struct ProcessSignalInfo info = {
        .sigInfo = sigInfo,
        .defaultTcb = NULL,
        .unblockedTcb = NULL,
        .awakenedTcb = NULL,
        .receivedTcb = NULL
    };

    if (info.sigInfo == NULL) {
        return -EFAULT;
    }

    /* visit all taskcb and dispatch signal */
    if (info.sigInfo->si_signo == SIGKILL) {
        OsSigAddSet(&spcb->sigShare, info.sigInfo->si_signo);
        (void)OsSigProcessForeachChild(spcb, SigProcessKillSigHandler, &info);
        return 0;
    } else {
        ret = OsSigProcessForeachChild(spcb, SigProcessSignalHandler, &info);
    }
    if (ret < 0) {
        return ret;
    }
    SigProcessLoadTcb(&info, sigInfo);
    return 0;
}

int OsSigEmptySet(sigset_t *set)
{
    *set = NULL_SIGNAL_SET;
    return 0;
}

/* Privilege process can't send to kernel and privilege process */
static int OsSignalPermissionToCheck(const LosProcessCB *spcb)
{
    UINT32 gid = spcb->group->groupID;

    if (gid == OS_KERNEL_PROCESS_GROUP) {
        return -EPERM;
    } else if (gid == OS_USER_PRIVILEGE_PROCESS_GROUP) {
        return -EPERM;
    }

    return 0;
}

int OsDispatch(pid_t pid, siginfo_t *info, int permission)
{
    if (OsProcessIDUserCheckInvalid(pid) || pid < 0) {
        return -ESRCH;
    }

    LosProcessCB *spcb = OS_PCB_FROM_PID(pid);
    if (OsProcessIsUnused(spcb)) {
        return -ESRCH;
    }

    /* If the process you want to kill had been inactive, but still exist. should return LOS_OK */
    if (OsProcessIsInactive(spcb)) {
        return LOS_OK;
    }

#ifdef LOSCFG_SECURITY_CAPABILITY
    LosProcessCB *current = OsCurrProcessGet();
    /* Kernel process always has kill permission and user process should check permission */
    if (OsProcessIsUserMode(current) && !(current->processStatus & OS_PROCESS_FLAG_EXIT)) {
        if ((current != spcb) && (!IsCapPermit(CAP_KILL)) && (current->user->userID != spcb->user->userID)) {
            return -EPERM;
        }
    }
#endif
    if ((permission == OS_USER_KILL_PERMISSION) && (OsSignalPermissionToCheck(spcb) < 0)) {
        return -EPERM;
    }
    return OsSigProcessSend(spcb, info);
}

int OsKill(pid_t pid, int sig, int permission)
{
    siginfo_t info;
    int ret;

    /* Make sure that the para is valid */
    if (!GOOD_SIGNO(sig)) {
        return -EINVAL;
    }

    /* Create the siginfo structure */
    info.si_signo = sig;
    info.si_code = SI_USER;
    info.si_value.sival_ptr = NULL;

    if (pid > 0) {
        /* Send the signal to the specify process */
        ret = OsDispatch(pid, &info, permission);
    } else if (pid == -1) {
        /* Send SIG to all processes */
        ret = OsSendSignalToAllProcess(&info, permission);
    } else {
        /* Send SIG to all processes in process group PGRP.
           If PGRP is zero, send SIG to all processes in
           the current process's process group. */
        ret = OsSendSignalToProcessGroup(pid, &info, permission);
    }
    return ret;
}

int OsKillLock(pid_t pid, int sig)
{
    int ret;
    unsigned int intSave;

    SCHEDULER_LOCK(intSave);
    ret = OsKill(pid, sig, OS_USER_KILL_PERMISSION);
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

INT32 OsTaskKillUnsafe(UINT32 taskID, INT32 signo)
{
    siginfo_t info;
    LosTaskCB *taskCB = OsGetTaskCB(taskID);
    INT32 ret = OsUserTaskOperatePermissionsCheck(taskCB);
    if (ret != LOS_OK) {
        return -ret;
    }

    /* Create the siginfo structure */
    info.si_signo = signo;
    info.si_code = SI_USER;
    info.si_value.sival_ptr = NULL;

    /* Dispatch the signal to thread, bypassing normal task group thread
     * dispatch rules. */
    return OsTcbDispatch(taskCB, &info);
}

int OsPthreadKill(UINT32 tid, int signo)
{
    int ret;
    UINT32 intSave;

    /* Make sure that the signal is valid */
    OS_RETURN_IF(!GOOD_SIGNO(signo), -EINVAL);
    if (OS_TID_CHECK_INVALID(tid)) {
        return -ESRCH;
    }

    /* Keep things stationary through the following */
    SCHEDULER_LOCK(intSave);
    ret = OsTaskKillUnsafe(tid, signo);
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

int OsSigAddSet(sigset_t *set, int signo)
{
    /* Verify the signal */
    if (!GOOD_SIGNO(signo)) {
        return -EINVAL;
    } else {
        /* In musl, sig No bits 00000100 present sig No 3, but  1<< 3 = 00001000, so signo needs minus 1 */
        signo -= 1;
        /* Add the signal to the set */
        *set |= SIGNO2SET((unsigned int)signo);
        return LOS_OK;
    }
}

int OsSigPending(sigset_t *set)
{
    LosTaskCB *tcb = NULL;
    unsigned int intSave;

    if (set == NULL) {
        return -EFAULT;
    }

    SCHEDULER_LOCK(intSave);
    tcb = OsCurrTaskGet();
    *set = tcb->sig.sigPendFlag;
    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
}

STATIC int FindFirstSetedBit(UINT64 n)
{
    int count;

    if (n == 0) {
        return -1;
    }
    for (count = 0; (count < UINT64_BIT_SIZE) && (n ^ 1ULL); n >>= 1, count++) {}
    return (count < UINT64_BIT_SIZE) ? count : (-1);
}

int OsSigTimedWaitNoLock(sigset_t *set, siginfo_t *info, unsigned int timeout)
{
    LosTaskCB *task = NULL;
    sig_cb *sigcb = NULL;
    int ret;

    task = OsCurrTaskGet();
    sigcb = &task->sig;

    if (sigcb->waitList.pstNext == NULL) {
        LOS_ListInit(&sigcb->waitList);
    }
    /* If pendingflag & set > 0, shound clear pending flag */
    sigset_t clear = sigcb->sigPendFlag & *set;
    if (clear) {
        sigcb->sigPendFlag ^= clear;
        ret = FindFirstSetedBit((UINT64)clear) + 1;
        OsMoveTmpInfoToUnbInfo(sigcb, ret);
    } else {
        OsSigAddSet(set, SIGKILL);
        OsSigAddSet(set, SIGSTOP);

        sigcb->sigwaitmask |= *set;
        OsTaskWaitSetPendMask(OS_TASK_WAIT_SIGNAL, sigcb->sigwaitmask, timeout);
        ret = OsSchedTaskWait(&sigcb->waitList, timeout, TRUE);
        if (ret == LOS_ERRNO_TSK_TIMEOUT) {
            ret = -EAGAIN;
        }
        sigcb->sigwaitmask = NULL_SIGNAL_SET;
    }
    if (info != NULL) {
        (VOID)memcpy_s(info, sizeof(siginfo_t), &sigcb->sigunbinfo, sizeof(siginfo_t));
    }
    return ret;
}

int OsSigTimedWait(sigset_t *set, siginfo_t *info, unsigned int timeout)
{
    int ret;
    unsigned int intSave;

    SCHEDULER_LOCK(intSave);

    ret = OsSigTimedWaitNoLock(set, info, timeout);

    SCHEDULER_UNLOCK(intSave);
    return ret;
}

int OsPause(void)
{
    LosTaskCB *spcb = NULL;
    sigset_t oldSigprocmask;

    spcb = OsCurrTaskGet();
    oldSigprocmask = spcb->sig.sigprocmask;
    return OsSigSuspend(&oldSigprocmask);
}

int OsSigSuspend(const sigset_t *set)
{
    unsigned int intSave;
    LosTaskCB *rtcb = NULL;
    sigset_t setSuspend;
    int ret;

    if (set == NULL) {
        return -EINVAL;
    }
    SCHEDULER_LOCK(intSave);
    rtcb = OsCurrTaskGet();

    /* Wait signal calc */
    setSuspend = FULL_SIGNAL_SET & (~(*set));

    /* If pending mask not in sigmask, need set sigflag */
    OsSigMaskSwitch(rtcb, *set);

    if (rtcb->sig.sigFlag > 0) {
        SCHEDULER_UNLOCK(intSave);

        /*
         * If rtcb->sig.sigFlag > 0, it means that some signal have been
         * received, and we need to do schedule to handle the signal directly.
         */
        LOS_Schedule();
        return -EINTR;
    } else {
        ret = OsSigTimedWaitNoLock(&setSuspend, NULL, LOS_WAIT_FOREVER);
        if (ret < 0) {
            PRINT_ERR("FUNC %s LINE = %d, ret = %x\n", __FUNCTION__, __LINE__, ret);
        }
    }

    SCHEDULER_UNLOCK(intSave);
    return -EINTR;
}

int OsSigAction(int sig, const sigaction_t *act, sigaction_t *oact)
{
    UINTPTR addr;
    sigaction_t action;

    if (!GOOD_SIGNO(sig) || sig < 1 || act == NULL) {
        return -EINVAL;
    }
    if (LOS_ArchCopyFromUser(&action, act, sizeof(sigaction_t)) != LOS_OK) {
        return -EFAULT;
    }

    if (sig == SIGSYS) {
        addr = OsGetSigHandler();
        if (addr == 0) {
            OsSetSigHandler((unsigned long)(UINTPTR)action.sa_handler);
            return LOS_OK;
        }
        return -EINVAL;
    }

    return LOS_OK;
}

VOID OsSigIntLock(VOID)
{
    LosTaskCB *task = OsCurrTaskGet();
    sig_cb *sigcb = &task->sig;

    (VOID)LOS_AtomicAdd((Atomic *)&sigcb->sigIntLock, 1);
}

VOID OsSigIntUnlock(VOID)
{
    LosTaskCB *task = OsCurrTaskGet();
    sig_cb *sigcb = &task->sig;

    (VOID)LOS_AtomicSub((Atomic *)&sigcb->sigIntLock, 1);
}

VOID *OsSaveSignalContext(VOID *sp, VOID *newSp)
{
    UINTPTR sigHandler;
    UINT32 intSave;

    LosTaskCB *task = OsCurrTaskGet();
    LosProcessCB *process = OsCurrProcessGet();
    sig_cb *sigcb = &task->sig;

    /* A thread is not allowed to interrupt the processing of its signals during a system call */
    if (sigcb->sigIntLock > 0) {
        return sp;
    }

    if (task->taskStatus & OS_TASK_FLAG_EXIT_KILL) {
        OsTaskToExit(task, 0);
        return sp;
    }

    SCHEDULER_LOCK(intSave);
    if ((sigcb->count == 0) && ((sigcb->sigFlag != 0) || (process->sigShare != 0))) {
        sigHandler = OsGetSigHandler();
        if (sigHandler == 0) {
            sigcb->sigFlag = 0;
            process->sigShare = 0;
            SCHEDULER_UNLOCK(intSave);
            PRINT_ERR("The signal processing function for the current process pid =%d is NULL!\n", task->processID);
            return sp;
        }
        /* One pthread do the share signal */
        sigcb->sigFlag |= process->sigShare;
        UINT32 signo = (UINT32)FindFirstSetedBit(sigcb->sigFlag) + 1;
        UINT32 sigVal = (UINT32)(UINTPTR)(sigcb->sigunbinfo.si_value.sival_ptr);
        OsMoveTmpInfoToUnbInfo(sigcb, signo);
        OsProcessExitCodeSignalSet(process, signo);
        sigcb->sigContext = sp;

        OsInitSignalContext(sp, newSp, sigHandler, signo, sigVal);

        /* sig No bits 00000100 present sig No 3, but  1<< 3 = 00001000, so signo needs minus 1 */
        sigcb->sigFlag ^= 1ULL << (signo - 1);
        sigcb->count++;
        SCHEDULER_UNLOCK(intSave);
        return newSp;
    }

    SCHEDULER_UNLOCK(intSave);
    return sp;
}

VOID *OsRestorSignalContext(VOID *sp)
{
    UINT32 intSave;

    LosTaskCB *task = OsCurrTaskGet();
    sig_cb *sigcb = &task->sig;

    SCHEDULER_LOCK(intSave);
    if (sigcb->count != 1) {
        SCHEDULER_UNLOCK(intSave);
        PRINT_ERR("sig error count : %d\n", sigcb->count);
        return sp;
    }

    LosProcessCB *process = OsCurrProcessGet();
    VOID *saveContext = sigcb->sigContext;
    sigcb->sigContext = NULL;
    sigcb->count--;
    process->sigShare = 0;
    OsProcessExitCodeSignalClear(process);
    SCHEDULER_UNLOCK(intSave);
    return saveContext;
}

#endif
