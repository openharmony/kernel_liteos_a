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

#include <sys/statfs.h>
#include <sys/mount.h>
#include "proc_fs.h"
#include "internal.h"
#ifdef LOSCFG_KERNEL_PM
#include "los_memory.h"
#include "los_pm.h"
#include "los_vm_map.h"
#include "user_copy.h"

static unsigned int PowerMemUserCopy(const char *src, size_t len, char **kbuf)
{
    if (LOS_IsUserAddressRange((VADDR_T)(UINTPTR)src, len)) {
        char *kernelBuf = LOS_MemAlloc(m_aucSysMem1, len + 1);
        if (kernelBuf == NULL) {
            return ENOMEM;
        }

        if (LOS_ArchCopyFromUser(kernelBuf, src, len) != 0) {
            (VOID)LOS_MemFree(m_aucSysMem1, kernelBuf);
            return EFAULT;
        }
        kernelBuf[len] = '\0';
        *kbuf = kernelBuf;
    }

    return 0;
}

static int PowerLockWrite(struct ProcFile *pf, const char *buf, size_t count, loff_t *ppos)
{
    char *kbuf = NULL;
    (void)pf;
    (void)ppos;

    if ((buf == NULL) || (count == 0)) {
        return -EINVAL;
    }

    unsigned ret = PowerMemUserCopy(buf, count, &kbuf);
    if (ret != 0) {
        return -(int)ret;
    } else if (kbuf != NULL) {
        buf = (const char *)kbuf;
    }

    ret = LOS_PmLockRequest(buf);
    (VOID)LOS_MemFree(m_aucSysMem1, kbuf);
    return -(int)ret;
}

static int PowerLockRead(struct SeqBuf *m, void *v)
{
    (void)v;

    LOS_PmLockInfoShow(m);
    return 0;
}

static const struct ProcFileOperations PowerLock = {
    .write      = PowerLockWrite,
    .read       = PowerLockRead,
};

static int PowerUnlockWrite(struct ProcFile *pf, const char *buf, size_t count, loff_t *ppos)
{
    char *kbuf = NULL;
    (void)pf;
    (void)ppos;

    if ((buf == NULL) || (count == 0)) {
        return -EINVAL;
    }

    unsigned ret = PowerMemUserCopy(buf, count, &kbuf);
    if (ret != 0) {
        return -(int)ret;
    } else if (kbuf != NULL) {
        buf = (const char *)kbuf;
    }

    ret = LOS_PmLockRelease(buf);
    (VOID)LOS_MemFree(m_aucSysMem1, kbuf);
    return -(int)ret;
}

static const struct ProcFileOperations PowerUnlock = {
    .write      = PowerUnlockWrite,
    .read       = PowerLockRead,
};

static int PowerModeWrite(struct ProcFile *pf, const char *buf, size_t count, loff_t *ppos)
{
    char *kbuf = NULL;
    (void)pf;
    (void)ppos;
    
    if ((buf == NULL) || (count == 0)) {
        return -EINVAL;
    }

    unsigned ret = PowerMemUserCopy(buf, count, &kbuf);
    if (ret != 0) {
        return -(int)ret;
    } else if (kbuf != NULL) {
        buf = (const char *)kbuf;
    }

    LOS_SysSleepEnum mode;

    if (buf == NULL) {
        return 0;
    }

    if (strcmp(buf, "normal") == 0) {
        mode = LOS_SYS_NORMAL_SLEEP;
    } else if (strcmp(buf, "light") == 0) {
        mode = LOS_SYS_LIGHT_SLEEP;
    } else if (strcmp(buf, "deep") == 0) {
        mode = LOS_SYS_DEEP_SLEEP;
    } else if (strcmp(buf, "shutdown") == 0) {
        mode = LOS_SYS_SHUTDOWN;
    } else {
        PRINT_ERR("Unsupported hibernation mode: %s\n", buf);
        (VOID)LOS_MemFree(m_aucSysMem1, kbuf);
        return -EINVAL;
    }

    ret = LOS_PmModeSet(mode);
    (VOID)LOS_MemFree(m_aucSysMem1, kbuf);
    return -(int)ret;
}

static int PowerModeRead(struct SeqBuf *m, void *v)
{
    (void)v;

    LosBufPrintf(m, "normal light deep shutdown\n");
    return 0;
}

static const struct ProcFileOperations PowerMode = {
    .write      = PowerModeWrite,
    .read       = PowerModeRead,
};

static int PowerCountRead(struct SeqBuf *m, void *v)
{
    (void)v;
    UINT32 count = LOS_PmReadLock();

    LosBufPrintf(m, "%u\n", count);
    return 0;
}

static int PowerCountWrite(struct ProcFile *pf, const char *buf, size_t count, loff_t *ppos)
{
    char *kbuf = NULL;
    (void)pf;
    (void)ppos;

    int weakCount;

    if ((buf == NULL) || (count == 0)) {
        return -EINVAL;
    }

    unsigned ret = PowerMemUserCopy(buf, count, &kbuf);
    if (ret != 0) {
        return -(int)ret;
    } else if (kbuf != NULL) {
        buf = (const char *)kbuf;
    }

    weakCount = atoi(buf);
    ret = LOS_PmSuspend(weakCount);
    (VOID)LOS_MemFree(m_aucSysMem1, kbuf);
    return -(int)ret;
}

static const struct ProcFileOperations PowerCount = {
    .write      = PowerCountWrite,
    .read       = PowerCountRead,
};

#define POWER_FILE_MODE  (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)
#define OS_POWER_PRIVILEGE 7

void ProcPmInit(void)
{
    struct ProcDirEntry *power = CreateProcEntry("power", S_IFDIR | S_IRWXU | S_IRWXG | S_IROTH, NULL);
    if (power == NULL) {
        PRINT_ERR("create /proc/power error!\n");
        return;
    }
    power->uid = OS_POWER_PRIVILEGE;
    power->gid = OS_POWER_PRIVILEGE;

    struct ProcDirEntry *mode = CreateProcEntry("power/power_mode", POWER_FILE_MODE, NULL);
    if (mode == NULL) {
        PRINT_ERR("create /proc/power/power_mode error!\n");
        goto FREE_POWER;
    }
    mode->procFileOps = &PowerMode;
    mode->uid = OS_POWER_PRIVILEGE;
    mode->gid = OS_POWER_PRIVILEGE;

    struct ProcDirEntry *lock = CreateProcEntry("power/power_lock", POWER_FILE_MODE, NULL);
    if (lock == NULL) {
        PRINT_ERR("create /proc/power/power_lock error!\n");
        goto FREE_MODE;
    }
    lock->procFileOps = &PowerLock;
    lock->uid = OS_POWER_PRIVILEGE;
    lock->gid = OS_POWER_PRIVILEGE;

    struct ProcDirEntry *unlock = CreateProcEntry("power/power_unlock", POWER_FILE_MODE, NULL);
    if (unlock == NULL) {
        PRINT_ERR("create /proc/power/power_unlock error!\n");
        goto FREE_LOCK;
    }
    unlock->procFileOps = &PowerUnlock;
    unlock->uid = OS_POWER_PRIVILEGE;
    unlock->gid = OS_POWER_PRIVILEGE;

    struct ProcDirEntry *count = CreateProcEntry("power/power_count", S_IRUSR | S_IRGRP | S_IROTH, NULL);
    if (count == NULL) {
        PRINT_ERR("create /proc/power/power_count error!\n");
        goto FREE_UNLOCK;
    }
    count->procFileOps = &PowerCount;
    count->uid = OS_POWER_PRIVILEGE;
    count->gid = OS_POWER_PRIVILEGE;

    return;

FREE_UNLOCK:
    ProcFreeEntry(unlock);
FREE_LOCK:
    ProcFreeEntry(lock);
FREE_MODE:
    ProcFreeEntry(mode);
FREE_POWER:
    ProcFreeEntry(power);
    return;
}
#endif
