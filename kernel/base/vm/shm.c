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

#include "stdio.h"
#include "string.h"
#include "time.h"
#include "sys/types.h"
#include "sys/shm.h"
#include "sys/stat.h"
#include "los_config.h"
#include "los_init.h"
#include "los_vm_map.h"
#include "los_vm_filemap.h"
#include "los_vm_phys.h"
#include "los_arch_mmu.h"
#include "los_vm_page.h"
#include "los_vm_lock.h"
#include "los_process.h"
#include "los_process_pri.h"
#include "user_copy.h"
#ifdef LOSCFG_SHELL
#include "shcmd.h"
#include "shell.h"
#endif


#ifdef LOSCFG_KERNEL_SHM

STATIC LosMux g_sysvShmMux;

/* private macro */
#define SYSV_SHM_LOCK()     (VOID)LOS_MuxLock(&g_sysvShmMux, LOS_WAIT_FOREVER)
#define SYSV_SHM_UNLOCK()   (VOID)LOS_MuxUnlock(&g_sysvShmMux)

/* The upper limit size of total shared memory is default 16M */
#define SHM_MAX_PAGES 4096
#define SHM_MAX (SHM_MAX_PAGES * PAGE_SIZE)
#define SHM_MIN 1
#define SHM_MNI 192
#define SHM_SEG 128
#define SHM_ALL (SHM_MAX_PAGES)

#define SHM_SEG_FREE    0x2000
#define SHM_SEG_USED    0x4000
#define SHM_SEG_REMOVE  0x8000

#ifndef SHM_M
#define SHM_M   010000
#endif

#ifndef SHM_X
#define SHM_X   0100
#endif

#ifndef ACCESSPERMS
#define ACCESSPERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#endif

#define SHM_S_IRUGO (S_IRUSR | S_IRGRP | S_IROTH)
#define SHM_S_IWUGO (S_IWUSR | S_IWGRP | S_IWOTH)
#define SHM_S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)

#define SHM_GROUPE_TO_USER  3
#define SHM_OTHER_TO_USER   6

struct shmIDSource {
    struct shmid_ds ds;
    UINT32 status;
    LOS_DL_LIST node;
#ifdef LOSCFG_SHELL
    CHAR ownerName[OS_PCB_NAME_LEN];
#endif
};

/* private data */
STATIC struct shminfo g_shmInfo = {
    .shmmax = SHM_MAX,
    .shmmin = SHM_MIN,
    .shmmni = SHM_MNI,
    .shmseg = SHM_SEG,
    .shmall = SHM_ALL,
};

STATIC struct shmIDSource *g_shmSegs = NULL;
STATIC UINT32 g_shmUsedPageCount;

UINT32 ShmInit(VOID)
{
    UINT32 ret;
    UINT32 i;

    ret = LOS_MuxInit(&g_sysvShmMux, NULL);
    if (ret != LOS_OK) {
        goto ERROR;
    }

    g_shmSegs = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, sizeof(struct shmIDSource) * g_shmInfo.shmmni);
    if (g_shmSegs == NULL) {
        (VOID)LOS_MuxDestroy(&g_sysvShmMux);
        goto ERROR;
    }
    (VOID)memset_s(g_shmSegs, (sizeof(struct shmIDSource) * g_shmInfo.shmmni),
                   0, (sizeof(struct shmIDSource) * g_shmInfo.shmmni));

    for (i = 0; i < g_shmInfo.shmmni; i++) {
        g_shmSegs[i].status = SHM_SEG_FREE;
        g_shmSegs[i].ds.shm_perm.seq = i + 1;
        LOS_ListInit(&g_shmSegs[i].node);
    }
    g_shmUsedPageCount = 0;

    return LOS_OK;

ERROR:
    VM_ERR("ShmInit fail\n");
    return LOS_NOK;
}

LOS_MODULE_INIT(ShmInit, LOS_INIT_LEVEL_VM_COMPLETE);

UINT32 ShmDeinit(VOID)
{
    UINT32 ret;

    (VOID)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, g_shmSegs);
    g_shmSegs = NULL;

    ret = LOS_MuxDestroy(&g_sysvShmMux);
    if (ret != LOS_OK) {
        return -1;
    }

    return 0;
}

STATIC inline VOID ShmSetSharedFlag(struct shmIDSource *seg)
{
    LosVmPage *page = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY(page, &seg->node, LosVmPage, node) {
        OsSetPageShared(page);
    }
}

STATIC inline VOID ShmClearSharedFlag(struct shmIDSource *seg)
{
    LosVmPage *page = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY(page, &seg->node, LosVmPage, node) {
        OsCleanPageShared(page);
    }
}

STATIC VOID ShmPagesRefDec(struct shmIDSource *seg)
{
    LosVmPage *page = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY(page, &seg->node, LosVmPage, node) {
        LOS_AtomicDec(&page->refCounts);
    }
}

STATIC INT32 ShmAllocSeg(key_t key, size_t size, INT32 shmflg)
{
    INT32 i;
    INT32 segNum = -1;
    struct shmIDSource *seg = NULL;
    size_t count;

    if ((size == 0) || (size < g_shmInfo.shmmin) ||
        (size > g_shmInfo.shmmax)) {
        return -EINVAL;
    }
    size = LOS_Align(size, PAGE_SIZE);
    if ((g_shmUsedPageCount + (size >> PAGE_SHIFT)) > g_shmInfo.shmall) {
        return -ENOMEM;
    }

    for (i = 0; i < g_shmInfo.shmmni; i++) {
        if (g_shmSegs[i].status & SHM_SEG_FREE) {
            g_shmSegs[i].status &= ~SHM_SEG_FREE;
            segNum = i;
            break;
        }
    }

    if (segNum < 0) {
        return -ENOSPC;
    }

    seg = &g_shmSegs[segNum];
    count = LOS_PhysPagesAlloc(size >> PAGE_SHIFT, &seg->node);
    if (count != (size >> PAGE_SHIFT)) {
        (VOID)LOS_PhysPagesFree(&seg->node);
        seg->status = SHM_SEG_FREE;
        return -ENOMEM;
    }
    ShmSetSharedFlag(seg);
    g_shmUsedPageCount += size >> PAGE_SHIFT;

    seg->status |= SHM_SEG_USED;
    seg->ds.shm_perm.mode = (UINT32)shmflg & ACCESSPERMS;
    seg->ds.shm_perm.key = key;
    seg->ds.shm_segsz = size;
    seg->ds.shm_perm.cuid = LOS_GetUserID();
    seg->ds.shm_perm.uid = LOS_GetUserID();
    seg->ds.shm_perm.cgid = LOS_GetGroupID();
    seg->ds.shm_perm.gid = LOS_GetGroupID();
    seg->ds.shm_lpid = 0;
    seg->ds.shm_nattch = 0;
    seg->ds.shm_cpid = LOS_GetCurrProcessID();
    seg->ds.shm_atime = 0;
    seg->ds.shm_dtime = 0;
    seg->ds.shm_ctime = time(NULL);
#ifdef LOSCFG_SHELL
    (VOID)memcpy_s(seg->ownerName, OS_PCB_NAME_LEN, OsCurrProcessGet()->processName, OS_PCB_NAME_LEN);
#endif

    return segNum;
}

STATIC INLINE VOID ShmFreeSeg(struct shmIDSource *seg)
{
    UINT32 count;

    ShmClearSharedFlag(seg);
    count = LOS_PhysPagesFree(&seg->node);
    if (count != (seg->ds.shm_segsz >> PAGE_SHIFT)) {
        VM_ERR("free physical pages failed, count = %d, size = %d", count, seg->ds.shm_segsz >> PAGE_SHIFT);
        return;
    }
    g_shmUsedPageCount -= seg->ds.shm_segsz >> PAGE_SHIFT;
    seg->status = SHM_SEG_FREE;
    LOS_ListInit(&seg->node);
}

STATIC INT32 ShmFindSegByKey(key_t key)
{
    INT32 i;
    struct shmIDSource *seg = NULL;

    for (i = 0; i < g_shmInfo.shmmni; i++) {
        seg = &g_shmSegs[i];
        if ((seg->status & SHM_SEG_USED) &&
            (seg->ds.shm_perm.key == key)) {
            return i;
        }
    }

    return -1;
}

STATIC INT32 ShmSegValidCheck(INT32 segNum, size_t size, INT32 shmFlg)
{
    struct shmIDSource *seg = &g_shmSegs[segNum];

    if (size > seg->ds.shm_segsz) {
        return -EINVAL;
    }

    if (((UINT32)shmFlg & (IPC_CREAT | IPC_EXCL)) ==
        (IPC_CREAT | IPC_EXCL)) {
        return -EEXIST;
    }

    return segNum;
}

STATIC struct shmIDSource *ShmFindSeg(int shmid)
{
    struct shmIDSource *seg = NULL;

    if ((shmid < 0) || (shmid >= g_shmInfo.shmmni)) {
        set_errno(EINVAL);
        return NULL;
    }

    seg = &g_shmSegs[shmid];
    if ((seg->status & SHM_SEG_FREE) || (seg->status & SHM_SEG_REMOVE)) {
        set_errno(EIDRM);
        return NULL;
    }

    return seg;
}

STATIC VOID ShmVmmMapping(LosVmSpace *space, LOS_DL_LIST *pageList, VADDR_T vaddr, UINT32 regionFlags)
{
    LosVmPage *vmPage = NULL;
    VADDR_T va = vaddr;
    PADDR_T pa;
    STATUS_T ret;

    LOS_DL_LIST_FOR_EACH_ENTRY(vmPage, pageList, LosVmPage, node) {
        pa = VM_PAGE_TO_PHYS(vmPage);
        LOS_AtomicInc(&vmPage->refCounts);
        ret = LOS_ArchMmuMap(&space->archMmu, va, pa, 1, regionFlags);
        if (ret != 1) {
            VM_ERR("LOS_ArchMmuMap failed, ret = %d", ret);
        }
        va += PAGE_SIZE;
    }
}

VOID OsShmFork(LosVmSpace *space, LosVmMapRegion *oldRegion, LosVmMapRegion *newRegion)
{
    struct shmIDSource *seg = NULL;

    SYSV_SHM_LOCK();
    seg = ShmFindSeg(oldRegion->shmid);
    if (seg == NULL) {
        SYSV_SHM_UNLOCK();
        VM_ERR("shm fork failed!");
        return;
    }

    newRegion->shmid = oldRegion->shmid;
    newRegion->forkFlags = oldRegion->forkFlags;
    ShmVmmMapping(space, &seg->node, newRegion->range.base, newRegion->regionFlags);
    seg->ds.shm_nattch++;
    SYSV_SHM_UNLOCK();
}

VOID OsShmRegionFree(LosVmSpace *space, LosVmMapRegion *region)
{
    struct shmIDSource *seg = NULL;

    SYSV_SHM_LOCK();
    seg = ShmFindSeg(region->shmid);
    if (seg == NULL) {
        SYSV_SHM_UNLOCK();
        return;
    }

    LOS_ArchMmuUnmap(&space->archMmu, region->range.base, region->range.size >> PAGE_SHIFT);
    ShmPagesRefDec(seg);
    seg->ds.shm_nattch--;
    if (seg->ds.shm_nattch <= 0 && (seg->status & SHM_SEG_REMOVE)) {
        ShmFreeSeg(seg);
    } else {
        seg->ds.shm_dtime = time(NULL);
        seg->ds.shm_lpid = LOS_GetCurrProcessID(); /* may not be the space's PID. */
    }
    SYSV_SHM_UNLOCK();
}

BOOL OsIsShmRegion(LosVmMapRegion *region)
{
    return (region->regionFlags & VM_MAP_REGION_FLAG_SHM) ? TRUE : FALSE;
}

STATIC INT32 ShmSegUsedCount(VOID)
{
    INT32 i;
    INT32 count = 0;
    struct shmIDSource *seg = NULL;

    for (i = 0; i < g_shmInfo.shmmni; i++) {
        seg = &g_shmSegs[i];
        if (seg->status & SHM_SEG_USED) {
            count++;
        }
    }
    return count;
}

STATIC INT32 ShmPermCheck(struct shmIDSource *seg, mode_t mode)
{
    INT32 uid = LOS_GetUserID();
    UINT32 tmpMode = 0;
    mode_t privMode = seg->ds.shm_perm.mode;
    mode_t accMode;

    if ((uid == seg->ds.shm_perm.uid) || (uid == seg->ds.shm_perm.cuid)) {
        tmpMode |= SHM_M;
        accMode = mode & S_IRWXU;
    } else if (LOS_CheckInGroups(seg->ds.shm_perm.gid) ||
               LOS_CheckInGroups(seg->ds.shm_perm.cgid)) {
        privMode <<= SHM_GROUPE_TO_USER;
        accMode = (mode & S_IRWXG) << SHM_GROUPE_TO_USER;
    } else {
        privMode <<= SHM_OTHER_TO_USER;
        accMode = (mode & S_IRWXO) << SHM_OTHER_TO_USER;
    }

    if (privMode & SHM_R) {
        tmpMode |= SHM_R;
    }

    if (privMode & SHM_W) {
        tmpMode |= SHM_W;
    }

    if (privMode & SHM_X) {
        tmpMode |= SHM_X;
    }

    if ((mode == SHM_M) && (tmpMode & SHM_M)) {
        return 0;
    } else if (mode == SHM_M) {
        return EACCES;
    }

    if ((tmpMode & accMode) == accMode) {
        return 0;
    } else {
        return EACCES;
    }
}

INT32 ShmGet(key_t key, size_t size, INT32 shmflg)
{
    INT32 ret;
    INT32 shmid;

    SYSV_SHM_LOCK();

    if (key == IPC_PRIVATE) {
        ret = ShmAllocSeg(key, size, shmflg);
    } else {
        ret = ShmFindSegByKey(key);
        if (ret < 0) {
            if (((UINT32)shmflg & IPC_CREAT) == 0) {
                ret = -ENOENT;
                goto ERROR;
            } else {
                ret = ShmAllocSeg(key, size, shmflg);
            }
        } else {
            shmid = ret;
            if (((UINT32)shmflg & IPC_CREAT) &&
                ((UINT32)shmflg & IPC_EXCL)) {
                ret = -EEXIST;
                goto ERROR;
            }
            ret = ShmPermCheck(ShmFindSeg(shmid), (UINT32)shmflg & ACCESSPERMS);
            if (ret != 0) {
                ret = -ret;
                goto ERROR;
            }
            ret = ShmSegValidCheck(shmid, size, shmflg);
        }
    }
    if (ret < 0) {
        goto ERROR;
    }

    SYSV_SHM_UNLOCK();

    return ret;
ERROR:
    set_errno(-ret);
    SYSV_SHM_UNLOCK();
    PRINT_DEBUG("%s %d, ret = %d\n", __FUNCTION__, __LINE__, ret);
    return -1;
}

INT32 ShmatParamCheck(const VOID *shmaddr, INT32 shmflg)
{
    if (((UINT32)shmflg & SHM_REMAP) && (shmaddr == NULL)) {
        return EINVAL;
    }

    if ((shmaddr != NULL) && !IS_PAGE_ALIGNED(shmaddr) &&
        (((UINT32)shmflg & SHM_RND) == 0)) {
        return EINVAL;
    }

    return 0;
}

LosVmMapRegion *ShmatVmmAlloc(struct shmIDSource *seg, const VOID *shmaddr,
                              INT32 shmflg, UINT32 prot)
{
    LosVmSpace *space = OsCurrProcessGet()->vmSpace;
    LosVmMapRegion *region = NULL;
    UINT32 flags = MAP_ANONYMOUS | MAP_SHARED;
    UINT32 mapFlags = flags | MAP_FIXED;
    VADDR_T vaddr;
    UINT32 regionFlags;
    INT32 ret;

    if (shmaddr != NULL) {
        flags |= MAP_FIXED_NOREPLACE;
    }
    regionFlags = OsCvtProtFlagsToRegionFlags(prot, flags);
    (VOID)LOS_MuxAcquire(&space->regionMux);
    if (shmaddr == NULL) {
        region = LOS_RegionAlloc(space, 0, seg->ds.shm_segsz, regionFlags, 0);
    } else {
        if ((UINT32)shmflg & SHM_RND) {
            vaddr = ROUNDDOWN((VADDR_T)(UINTPTR)shmaddr, SHMLBA);
        } else {
            vaddr = (VADDR_T)(UINTPTR)shmaddr;
        }
        if (!((UINT32)shmflg & SHM_REMAP) && (LOS_RegionFind(space, vaddr) ||
            LOS_RegionFind(space, vaddr + seg->ds.shm_segsz - 1) ||
            LOS_RegionRangeFind(space, vaddr, seg->ds.shm_segsz - 1))) {
            ret = EINVAL;
            goto ERROR;
        }
        vaddr = (VADDR_T)LOS_MMap(vaddr, seg->ds.shm_segsz, prot, mapFlags, -1, 0);
        region = LOS_RegionFind(space, vaddr);
    }

    if (region == NULL) {
        ret = ENOMEM;
        goto ERROR;
    }
    ShmVmmMapping(space, &seg->node, region->range.base, regionFlags);
    (VOID)LOS_MuxRelease(&space->regionMux);
    return region;
ERROR:
    set_errno(ret);
    (VOID)LOS_MuxRelease(&space->regionMux);
    return NULL;
}

VOID *ShmAt(INT32 shmid, const VOID *shmaddr, INT32 shmflg)
{
    INT32 ret;
    UINT32 prot = PROT_READ;
    mode_t acc_mode = SHM_S_IRUGO;
    struct shmIDSource *seg = NULL;
    LosVmMapRegion *r = NULL;

    ret = ShmatParamCheck(shmaddr, shmflg);
    if (ret != 0) {
        set_errno(ret);
        return (VOID *)-1;
    }

    if ((UINT32)shmflg & SHM_EXEC) {
        prot |= PROT_EXEC;
        acc_mode |= SHM_S_IXUGO;
    } else if (((UINT32)shmflg & SHM_RDONLY) == 0) {
        prot |= PROT_WRITE;
        acc_mode |= SHM_S_IWUGO;
    }

    SYSV_SHM_LOCK();
    seg = ShmFindSeg(shmid);
    if (seg == NULL) {
        SYSV_SHM_UNLOCK();
        return (VOID *)-1;
    }

    ret = ShmPermCheck(seg, acc_mode);
    if (ret != 0) {
        goto ERROR;
    }

    seg->ds.shm_nattch++;
    r = ShmatVmmAlloc(seg, shmaddr, shmflg, prot);
    if (r == NULL) {
        seg->ds.shm_nattch--;
        SYSV_SHM_UNLOCK();
        return (VOID *)-1;
    }

    r->shmid = shmid;
    r->regionFlags |= VM_MAP_REGION_FLAG_SHM;
    seg->ds.shm_atime = time(NULL);
    seg->ds.shm_lpid = LOS_GetCurrProcessID();
    SYSV_SHM_UNLOCK();

    return (VOID *)(UINTPTR)r->range.base;
ERROR:
    set_errno(ret);
    SYSV_SHM_UNLOCK();
    PRINT_DEBUG("%s %d, ret = %d\n", __FUNCTION__, __LINE__, ret);
    return (VOID *)-1;
}

INT32 ShmCtl(INT32 shmid, INT32 cmd, struct shmid_ds *buf)
{
    struct shmIDSource *seg = NULL;
    INT32 ret = 0;
    struct shm_info shmInfo;
    struct ipc_perm shm_perm;

    cmd = ((UINT32)cmd & ~IPC_64);

    SYSV_SHM_LOCK();

    if ((cmd != IPC_INFO) && (cmd != SHM_INFO)) {
        seg = ShmFindSeg(shmid);
        if (seg == NULL) {
            SYSV_SHM_UNLOCK();
            return -1;
        }
    }

    if ((buf == NULL) && (cmd != IPC_RMID)) {
        ret = EINVAL;
        goto ERROR;
    }

    switch (cmd) {
        case IPC_STAT:
        case SHM_STAT:
            ret = ShmPermCheck(seg, SHM_S_IRUGO);
            if (ret != 0) {
                goto ERROR;
            }

            ret = LOS_ArchCopyToUser(buf, &seg->ds, sizeof(struct shmid_ds));
            if (ret != 0) {
                ret = EFAULT;
                goto ERROR;
            }
            if (cmd == SHM_STAT) {
                ret = (unsigned int)((unsigned int)seg->ds.shm_perm.seq << 16) | (unsigned int)((unsigned int)shmid & 0xffff); /* 16: use the seq as the upper 16 bits */
            }
            break;
        case IPC_SET:
            ret = ShmPermCheck(seg, SHM_M);
            if (ret != 0) {
                ret = EPERM;
                goto ERROR;
            }

            ret = LOS_ArchCopyFromUser(&shm_perm, &buf->shm_perm, sizeof(struct ipc_perm));
            if (ret != 0) {
                ret = EFAULT;
                goto ERROR;
            }
            seg->ds.shm_perm.uid = shm_perm.uid;
            seg->ds.shm_perm.gid = shm_perm.gid;
            seg->ds.shm_perm.mode = (seg->ds.shm_perm.mode & ~ACCESSPERMS) |
                                    (shm_perm.mode & ACCESSPERMS);
            seg->ds.shm_ctime = time(NULL);
#ifdef LOSCFG_SHELL
            (VOID)memcpy_s(seg->ownerName, OS_PCB_NAME_LEN, OS_PCB_FROM_PID(shm_perm.uid)->processName,
                           OS_PCB_NAME_LEN);
#endif
            break;
        case IPC_RMID:
            ret = ShmPermCheck(seg, SHM_M);
            if (ret != 0) {
                ret = EPERM;
                goto ERROR;
            }

            seg->status |= SHM_SEG_REMOVE;
            if (seg->ds.shm_nattch <= 0) {
                ShmFreeSeg(seg);
            }
            break;
        case IPC_INFO:
            ret = LOS_ArchCopyToUser(buf, &g_shmInfo, sizeof(struct shminfo));
            if (ret != 0) {
                ret = EFAULT;
                goto ERROR;
            }
            ret = g_shmInfo.shmmni;
            break;
        case SHM_INFO:
            shmInfo.shm_rss = 0;
            shmInfo.shm_swp = 0;
            shmInfo.shm_tot = 0;
            shmInfo.swap_attempts = 0;
            shmInfo.swap_successes = 0;
            shmInfo.used_ids = ShmSegUsedCount();
            ret = LOS_ArchCopyToUser(buf, &shmInfo, sizeof(struct shm_info));
            if (ret != 0) {
                ret = EFAULT;
                goto ERROR;
            }
            ret = g_shmInfo.shmmni;
            break;
        default:
            VM_ERR("the cmd(%d) is not supported!", cmd);
            ret = EINVAL;
            goto ERROR;
    }

    SYSV_SHM_UNLOCK();
    return ret;

ERROR:
    set_errno(ret);
    SYSV_SHM_UNLOCK();
    PRINT_DEBUG("%s %d, ret = %d\n", __FUNCTION__, __LINE__, ret);
    return -1;
}

INT32 ShmDt(const VOID *shmaddr)
{
    LosVmSpace *space = OsCurrProcessGet()->vmSpace;
    struct shmIDSource *seg = NULL;
    LosVmMapRegion *region = NULL;
    INT32 shmid;
    INT32 ret;

    if (IS_PAGE_ALIGNED(shmaddr) == 0) {
        ret = EINVAL;
        goto ERROR;
    }

    (VOID)LOS_MuxAcquire(&space->regionMux);
    region = LOS_RegionFind(space, (VADDR_T)(UINTPTR)shmaddr);
    if (region == NULL) {
        ret = EINVAL;
        goto ERROR_WITH_LOCK;
    }
    shmid = region->shmid;

    if (region->range.base != (VADDR_T)(UINTPTR)shmaddr) {
        ret = EINVAL;
        goto ERROR_WITH_LOCK;
    }

    /* remove it from aspace */
    LOS_RbDelNode(&space->regionRbTree, &region->rbNode);
    LOS_ArchMmuUnmap(&space->archMmu, region->range.base, region->range.size >> PAGE_SHIFT);
    /* free it */
    free(region);

    SYSV_SHM_LOCK();
    seg = ShmFindSeg(shmid);
    if (seg == NULL) {
        ret = EINVAL;
        SYSV_SHM_UNLOCK();
        goto ERROR_WITH_LOCK;
    }

    ShmPagesRefDec(seg);
    seg->ds.shm_nattch--;
    if ((seg->ds.shm_nattch <= 0) &&
        (seg->status & SHM_SEG_REMOVE)) {
        ShmFreeSeg(seg);
    } else {
        seg->ds.shm_dtime = time(NULL);
        seg->ds.shm_lpid = LOS_GetCurrProcessID();
    }
    SYSV_SHM_UNLOCK();
    (VOID)LOS_MuxRelease(&space->regionMux);
    return 0;

ERROR_WITH_LOCK:
    (VOID)LOS_MuxRelease(&space->regionMux);
ERROR:
    set_errno(ret);
    PRINT_DEBUG("%s %d, ret = %d\n", __FUNCTION__, __LINE__, ret);
    return -1;
}

#ifdef LOSCFG_SHELL
STATIC VOID OsShmInfoCmd(VOID)
{
    INT32 i;
    struct shmIDSource *seg = NULL;

    PRINTK("\r\n------- Shared Memory Segments -------\n");
    PRINTK("key      shmid    perms      bytes      nattch     status     owner\n");
    SYSV_SHM_LOCK();
    for (i = 0; i < g_shmInfo.shmmni; i++) {
        seg = &g_shmSegs[i];
        if (!(seg->status & SHM_SEG_USED)) {
            continue;
        }
        PRINTK("%08x %-8d %-10o %-10u %-10u %-10x %s\n", seg->ds.shm_perm.key,
               i, seg->ds.shm_perm.mode, seg->ds.shm_segsz, seg->ds.shm_nattch,
               seg->status, seg->ownerName);

    }
    SYSV_SHM_UNLOCK();
}

STATIC VOID OsShmDeleteCmd(INT32 shmid)
{
    struct shmIDSource *seg = NULL;

    if ((shmid < 0) || (shmid >= g_shmInfo.shmmni)) {
        PRINT_ERR("shmid is invalid: %d\n", shmid);
        return;
    }

    SYSV_SHM_LOCK();
    seg = ShmFindSeg(shmid);
    if (seg == NULL) {
        SYSV_SHM_UNLOCK();
        return;
    }

    if (seg->ds.shm_nattch <= 0) {
        ShmFreeSeg(seg);
    }
    SYSV_SHM_UNLOCK();
}

STATIC VOID OsShmCmdUsage(VOID)
{
    PRINTK("\tnone option,   print shm usage info\n"
           "\t-r [shmid],    Recycle the specified shared memory about shmid\n"
           "\t-h | --help,   print shm command usage\n");
}

UINT32 OsShellCmdShm(INT32 argc, const CHAR *argv[])
{
    INT32 shmid;
    CHAR *endPtr = NULL;

    if (argc == 0) {
        OsShmInfoCmd();
    } else if (argc == 1) {
        if ((strcmp(argv[0], "-h") != 0) && (strcmp(argv[0], "--help") != 0)) {
            PRINTK("Invalid option: %s\n", argv[0]);
        }
        OsShmCmdUsage();
    } else if (argc == 2) { /* 2: two parameter */
        if (strcmp(argv[0], "-r") != 0) {
            PRINTK("Invalid option: %s\n", argv[0]);
            goto DONE;
        }
        shmid = strtoul((CHAR *)argv[1], &endPtr, 0);
        if ((endPtr == NULL) || (*endPtr != 0)) {
            PRINTK("check shmid %s(us) invalid\n", argv[1]);
            goto DONE;
        }
        /* try to delete shm about shmid */
        OsShmDeleteCmd(shmid);
    }
    return 0;
DONE:
    OsShmCmdUsage();
    return -1;
}

SHELLCMD_ENTRY(shm_shellcmd, CMD_TYPE_SHOW, "shm", 2, (CmdCallBackFunc)OsShellCmdShm);
#endif
#endif

