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

#include "internal.h"
#include "proc_fs.h"
#include "vnode.h"
#include "path_cache.h"
#include "los_vm_filemap.h"
static char* VnodeTypeToStr(enum VnodeType type) {
    switch (type) {
        case VNODE_TYPE_UNKNOWN:
            return "UKN";
        case VNODE_TYPE_REG:
            return "REG";
        case VNODE_TYPE_DIR:
            return "DIR";
        case VNODE_TYPE_BLK:
            return "BLK";
        case VNODE_TYPE_CHR:
            return "CHR";
        case VNODE_TYPE_BCHR:
            return "BCH";
        case VNODE_TYPE_FIFO:
            return "FIF";
        case VNODE_TYPE_LNK:
            return "LNK";
        default:
            return "BAD";
    }
}

static int VnodeListProcess(struct SeqBuf *buf, LIST_HEAD* list)
{
    int count = 0;
    struct Vnode *item = NULL;
    struct Vnode *nextItem = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, list, struct Vnode, actFreeEntry) {
        LosBufPrintf(buf, "%-10p    %-10p     %-10p    %10p    0x%08x    %-3d    %-4s    %-3d    %-3d    %o\n",
            item, item->parent, item->data, item->vop, item->hash, item->useCount,
            VnodeTypeToStr(item->type), item->gid, item->uid, item->mode);
        count++;
    }

    return count;
}

static int PathCacheListProcess(struct SeqBuf *buf)
{
    int count = 0;
    LIST_HEAD* bucketList = GetPathCacheList();

    for (int i = 0; i < LOSCFG_MAX_PATH_CACHE_SIZE; i++) {
        struct PathCache *pc = NULL;
        LIST_HEAD *list = &bucketList[i];

        LOS_DL_LIST_FOR_EACH_ENTRY(pc, list, struct PathCache, hashEntry) {
            LosBufPrintf(buf, "%-3d    %-10p    %-11p    %-10p    %-9d    %s\n", i, pc,
                pc->parentVnode, pc->childVnode, pc->hit, pc->name);
            count++;
        }
    }

    return count;
}

static void PageCacheEntryProcess(struct SeqBuf *buf, struct page_mapping *mapping)
{
    LosFilePage *fpage = NULL;

    if (mapping == NULL) {
        LosBufPrintf(buf, "null]\n");
        return;
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(fpage, &mapping->page_list, LosFilePage, node) {
        LosBufPrintf(buf, "%d,", fpage->pgoff);
    }
    LosBufPrintf(buf, "]\n");
    return;
}

static void PageCacheMapProcess(struct SeqBuf *buf)
{
    struct filelist *flist = &tg_filelist;
    struct file *filep = NULL;

    (void)sem_wait(&flist->fl_sem);
    for (int i = 3; i < CONFIG_NFILE_DESCRIPTORS; i++) {
        if (!get_bit(i)) {
            continue;
        }
        filep = &flist->fl_files[i];
        if (filep == NULL) {
            continue;
        }
        LosBufPrintf(buf, "[%d]%s:[", i, filep->f_path);
        PageCacheEntryProcess(buf, filep->f_mapping);
    }
    (void)sem_post(&flist->fl_sem);
}

static int FsCacheInfoFill(struct SeqBuf *buf, void *arg)
{
    int vnodeFree= 0;
    int vnodeActive= 0;
    int vnodeVirtual= 0;
    int vnodeTotal = 0;

    int pathCacheTotal = 0;
    int pathCacheTotalTry = 0;
    int pathCacheTotalHit = 0;

    int pageCacheTotalTry = 0;
    int pageCacheTotalHit = 0;

    ResetPathCacheHitInfo(&pathCacheTotalHit, &pathCacheTotalTry);
    ResetPageCacheHitInfo(&pageCacheTotalTry, &pageCacheTotalHit);

    VnodeHold();
    LosBufPrintf(buf, "\n=================================================================\n");
    LosBufPrintf(buf, "VnodeAddr     ParentAddr     DataAddr      VnodeOps      Hash          Ref    Type    Gid    Uid    Mode\n");
    vnodeVirtual = VnodeListProcess(buf, GetVnodeVirtualList());
    vnodeFree = VnodeListProcess(buf, GetVnodeFreeList());
    vnodeActive = VnodeListProcess(buf, GetVnodeActiveList());
    vnodeTotal = vnodeVirtual + vnodeFree + vnodeActive;

    LosBufPrintf(buf, "\n=================================================================\n");
    LosBufPrintf(buf, "No.    CacheAddr     ParentAddr     ChildAddr     HitCount     Name\n");
    pathCacheTotal = PathCacheListProcess(buf);

    LosBufPrintf(buf, "\n=================================================================\n");
    PageCacheMapProcess(buf);
    LosBufPrintf(buf, "\n=================================================================\n");
    LosBufPrintf(buf, "PathCache Total:%d Try:%d Hit:%d\n",
        pathCacheTotal, pathCacheTotalTry, pathCacheTotalHit);
    LosBufPrintf(buf, "Vnode Total:%d Free:%d Virtual:%d Active:%d\n",
        vnodeTotal, vnodeFree, vnodeVirtual, vnodeActive);
    LosBufPrintf(buf, "PageCache Try:%d Hit:%d\n", pageCacheTotalTry, pageCacheTotalHit);
    VnodeDrop();
    return 0;
}

static const struct ProcFileOperations FS_CACHE_PROC_FOPS = {
    .read = FsCacheInfoFill,
};

void ProcFsCacheInit(void)
{
#ifdef LOSCFG_DEBUG_VERSION
    struct ProcDirEntry *pde = CreateProcEntry("fs_cache", 0, NULL);
    if (pde == NULL) {
        PRINT_ERR("create fs_cache error!\n");
        return;
    }

    pde->procFileOps = &FS_CACHE_PROC_FOPS;
#endif
}
