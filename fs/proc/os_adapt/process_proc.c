/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2023 Huawei Device Co., Ltd. All rights reserved.
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
#include "los_process_pri.h"

#ifdef LOSCFG_PROC_PROCESS_DIR
typedef enum {
    PROC_PID,
} ProcessDataType;

struct ProcProcess {
    char         *name;
    mode_t       mode;
    int          type;
    const struct ProcFileOperations *fileOps;
};

struct ProcessData {
    uintptr_t process;
    unsigned int type;
};

#define PROC_PID_PRIVILEGE 7
#define PROC_PID_DIR_LEN 100
#ifdef LOSCFG_KERNEL_CONTAINER
static ssize_t ProcessContainerLink(unsigned int containerID, ContainerType type, char *buffer, size_t bufLen)
{
    ssize_t count = -1;
    if (type == PID_CONTAINER) {
        count = snprintf_s(buffer, bufLen, bufLen - 1, "'pid:[%u]'", containerID);
    } else if (type == UTS_CONTAINER) {
        count = snprintf_s(buffer, bufLen, bufLen - 1, "'uts:[%u]'", containerID);
    }

    if (count < 0) {
        return -EBADF;
    }
    return count;
}

static ssize_t ProcessContainerReadLink(struct ProcDirEntry *entry, char *buffer, size_t bufLen)
{
    ssize_t count;
    unsigned int intSave;
    if (entry == NULL) {
        return -EINVAL;
    }
    struct ProcessData *data = (struct ProcessData *)entry->data;
    if (data == NULL) {
        return -EINVAL;
    }
    LosProcessCB *processCB = (LosProcessCB *)data->process;
    SCHEDULER_LOCK(intSave);
    UINT32 containerID = OsGetContainerID(processCB->container, (ContainerType)data->type);
    SCHEDULER_UNLOCK(intSave);
    if (containerID != OS_INVALID_VALUE) {
        return ProcessContainerLink(containerID, (ContainerType)data->type, buffer, bufLen);
    }
    count = strlen("(unknown)");
    if (memcpy_s(buffer, bufLen, "(unknown)", count + 1) != EOK) {
        return -EBADF;
    }
    return count;
}

static const struct ProcFileOperations PID_CONTAINER_FOPS = {
    .readLink = ProcessContainerReadLink,
};
#endif /* LOSCFG_KERNEL_CONTAINER */

static int ProcProcessRead(struct SeqBuf *m, void *v)
{
    (void)m;
    (void)v;
    return -EINVAL;
}

static const struct ProcFileOperations PID_FOPS = {
    .read = ProcProcessRead,
};

static struct ProcProcess g_procProcess[] = {
    {
        .name = NULL,
        .mode = S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH,
        .type = PROC_PID,
        .fileOps = &PID_FOPS

    },
#ifdef LOSCFG_KERNEL_CONTAINER
    {
        .name = "container",
        .mode = S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH,
        .type = CONTAINER,
        .fileOps = &PID_CONTAINER_FOPS

    },
#ifdef LOSCFG_PID_CONTAINER
    {
        .name = "container/pid",
        .mode = S_IFLNK,
        .type = PID_CONTAINER,
        .fileOps = &PID_CONTAINER_FOPS
    },
#endif
#ifdef LOSCFG_UTS_CONTAINER
    {
        .name = "container/uts",
        .mode = S_IFLNK,
        .type = UTS_CONTAINER,
        .fileOps = &PID_CONTAINER_FOPS
    },
#endif
#endif
};

void ProcFreeProcessDir(struct ProcDirEntry *processDir)
{
    if (processDir == NULL) {
        return;
    }
    RemoveProcEntry(processDir->name, NULL);
}

static struct ProcDirEntry *ProcCreatePorcess(UINT32 pid, struct ProcProcess *porcess, uintptr_t processCB)
{
    int ret;
    char pidName[PROC_PID_DIR_LEN] = {0};
    struct ProcessData *data = (struct ProcessData *)malloc(sizeof(struct ProcessData));
    if (data == NULL) {
        return NULL;
    }
    if (porcess->name != NULL) {
        ret = snprintf_s(pidName, PROC_PID_DIR_LEN, PROC_PID_DIR_LEN - 1, "%u/%s", pid, porcess->name);
    } else {
        ret = snprintf_s(pidName, PROC_PID_DIR_LEN, PROC_PID_DIR_LEN - 1, "%u", pid);
    }
    if (ret < 0) {
        free(data);
        return NULL;
    }

    data->process = processCB;
    data->type = porcess->type;
    struct ProcDirEntry *container = ProcCreateData(pidName, porcess->mode, NULL, porcess->fileOps, (void *)data);
    if (container == NULL) {
        free(data);
        PRINT_ERR("create /proc/%s error!\n", pidName);
        return NULL;
    }
    return container;
}

int ProcCreateProcessDir(UINT32 pid, uintptr_t process)
{
    unsigned int intSave;
    struct ProcDirEntry *pidDir = NULL;
    for (int index = 0; index < (sizeof(g_procProcess) / sizeof(struct ProcProcess)); index++) {
        struct ProcProcess *procProcess = &g_procProcess[index];
        struct ProcDirEntry *dir = ProcCreatePorcess(pid, procProcess, process);
        if (dir == NULL) {
            PRINT_ERR("create /proc/%s error!\n", procProcess->name);
            goto CREATE_ERROR;
        }
        if (index == 0) {
            pidDir = dir;
        }
    }

    SCHEDULER_LOCK(intSave);
    ((LosProcessCB *)process)->procDir = pidDir;
    SCHEDULER_UNLOCK(intSave);
    return 0;

CREATE_ERROR:
    if (pidDir != NULL) {
        RemoveProcEntry(pidDir->name, NULL);
    }
    return -1;
}
#endif /* LOSCFG_PROC_PROCESS_DIR */

static int ProcessProcFill(struct SeqBuf *m, void *v)
{
    (void)v;
    (void)OsShellCmdTskInfoGet(OS_ALL_TASK_MASK, m, OS_PROCESS_INFO_ALL);
    return 0;
}

static const struct ProcFileOperations PROCESS_PROC_FOPS = {
    .read       = ProcessProcFill,
};

void ProcProcessInit(void)
{
    struct ProcDirEntry *pde = CreateProcEntry("process", 0, NULL);
    if (pde == NULL) {
        PRINT_ERR("create /proc/process error!\n");
        return;
    }
    pde->procFileOps = &PROCESS_PROC_FOPS;

#ifdef LOSCFG_PROC_PROCESS_DIR
    int ret = ProcCreateProcessDir(OS_USER_ROOT_PROCESS_ID, (uintptr_t)OsGetUserInitProcess());
    if (ret < 0) {
        PRINT_ERR("Create proc process %d dir failed!\n", OS_USER_ROOT_PROCESS_ID);
    }
#endif
    return;
}
