/*
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd. All rights reserved.
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

#include "proc_file.h"

#include <sys/statfs.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "fs/dirent_fs.h"
#include "fs/mount.h"
#include "fs/fs.h"
#include "los_tables.h"
#include "internal.h"

#ifdef LOSCFG_FS_PROC
static struct VnodeOps g_procfsVops;
static struct file_operations_vfs g_procfsFops;

struct ProcDirEntry *VnodeToEntry(struct Vnode *node)
{
    return (struct ProcDirEntry *)(node->data);
}

static struct Vnode *EntryToVnode(struct ProcDirEntry *entry)
{
    struct Vnode *node = NULL;

    (void)VnodeAlloc(&g_procfsVops, &node);
    node->fop = &g_procfsFops;
    node->data = entry;
    node->type = entry->type;
    node->uid = entry->uid;
    node->gid = entry->gid;
    node->mode = entry->mode;
    return node;
}

static int EntryMatch(const char *name, int len, const struct ProcDirEntry *pn)
{
    if (len != pn->nameLen) {
        return 0;
    }
    return !strncmp(name, pn->name, len);
}

int VfsProcfsTruncate(struct Vnode *pVnode, off_t len)
{
    return 0;
}

int VfsProcfsCreate(struct Vnode* parent, const char *name, int mode, struct Vnode **vnode)
{
    int ret;
    struct Vnode *vp = NULL;
    struct ProcDirEntry *curEntry = NULL;

    struct ProcDirEntry *parentEntry = VnodeToEntry(parent);
    if (parentEntry == NULL) {
        return -ENODATA;
    }

    ret = VnodeAlloc(&g_procfsVops, &vp);
    if (ret != 0) {
        return -ENOMEM;
    }

    curEntry = ProcCreate(name, mode, parentEntry, NULL);
    if (curEntry == NULL) {
        VnodeFree(vp);
        return -ENODATA;
    }

    vp->data = curEntry;
    vp->type = curEntry->type;
    if (vp->type == VNODE_TYPE_DIR) {
        vp->mode = S_IFDIR | PROCFS_DEFAULT_MODE;
    } else {
        vp->mode = S_IFREG | PROCFS_DEFAULT_MODE;
    }

    vp->vop = parent->vop;
    vp->fop = parent->fop;
    vp->parent = parent;
    vp->originMount = parent->originMount;

    *vnode = vp;

    return LOS_OK;
}

int VfsProcfsRead(struct file *filep, char *buffer, size_t buflen)
{
    ssize_t size;
    struct ProcDirEntry *entry = NULL;
    if ((filep == NULL) || (filep->f_vnode == NULL) || (buffer == NULL)) {
        return -EINVAL;
    }

    VnodeHold();
    entry = VnodeToEntry(filep->f_vnode);
    if (entry == NULL) {
        VnodeDrop();
        return -EPERM;
    }

    size = (ssize_t)ReadProcFile(entry, (void *)buffer, buflen);
    filep->f_pos = entry->pf->fPos;
    VnodeDrop();
    return size;
}

int VfsProcfsWrite(struct file *filep, const char *buffer, size_t buflen)
{
    ssize_t size;
    struct ProcDirEntry *entry = NULL;
    if ((filep == NULL) || (filep->f_vnode == NULL) || (buffer == NULL)) {
        return -EINVAL;
    }

    VnodeHold();
    entry = VnodeToEntry(filep->f_vnode);
    if (entry == NULL) {
        VnodeDrop();
        return -EPERM;
    }

    size = (ssize_t)WriteProcFile(entry, (void *)buffer, buflen);
    filep->f_pos = entry->pf->fPos;
    VnodeDrop();
    return size;
}

int VfsProcfsLookup(struct Vnode *parent, const char *name, int len, struct Vnode **vpp)
{
    if (parent == NULL || name == NULL || len <= 0 || vpp == NULL) {
        return -EINVAL;
    }
    struct ProcDirEntry *entry = VnodeToEntry(parent);
    if (entry == NULL) {
        return -ENODATA;
    }

    entry = entry->subdir;
    while (1) {
        if (entry == NULL) {
            return -ENOENT;
        }
        if (EntryMatch(name, len, entry)) {
            break;
        }
        entry = entry->next;
    }

    *vpp = EntryToVnode(entry);
    if ((*vpp) == NULL) {
        return -ENOMEM;
    }
    (*vpp)->originMount = parent->originMount;
    (*vpp)->parent = parent;
    return LOS_OK;
}

int VfsProcfsMount(struct Mount *mnt, struct Vnode *device, const void *data)
{
    struct Vnode *vp = NULL;
    int ret;

    spin_lock_init(&procfsLock);
    procfsInit = true;

    ret = VnodeAlloc(&g_procfsVops, &vp);
    if (ret != 0) {
        return -ENOMEM;
    }

    struct ProcDirEntry *root = GetProcRootEntry();
    vp->data = root;
    vp->originMount = mnt;
    vp->fop = &g_procfsFops;
    mnt->data = NULL;
    mnt->vnodeCovered = vp;
    vp->type = root->type;
    if (vp->type == VNODE_TYPE_DIR) {
        vp->mode = S_IFDIR | PROCFS_DEFAULT_MODE;
    } else {
        vp->mode = S_IFREG | PROCFS_DEFAULT_MODE;
    }

    return LOS_OK;
}

int VfsProcfsUnmount(void *handle, struct Vnode **blkdriver)
{
    (void)handle;
    (void)blkdriver;
    return -EPERM;
}

int VfsProcfsStat(struct Vnode *node, struct stat *buf)
{
    VnodeHold();
    struct ProcDirEntry *entry = VnodeToEntry(node);
    if (entry == NULL) {
        VnodeDrop();
        return -EPERM;
    }
    (void)memset_s(buf, sizeof(struct stat), 0, sizeof(struct stat));
    buf->st_mode = entry->mode;
    VnodeDrop();
    return LOS_OK;
}

#ifdef LOSCFG_KERNEL_PLIMITS
int VfsProcfsMkdir(struct Vnode *parent, const char *dirName, mode_t mode, struct Vnode **vnode)
{
    struct ProcDirEntry *parentEntry = VnodeToEntry(parent);
    struct ProcDirEntry *pde = NULL;
    if ((parentEntry->procDirOps == NULL) || (parentEntry->procDirOps->mkdir == NULL)) {
        return -ENOSYS;
    }

    int ret = parentEntry->procDirOps->mkdir(parentEntry, dirName, mode, &pde);
    if ((ret < 0) || (pde == NULL)) {
        return ret;
    }

    *vnode = EntryToVnode(pde);
    (*vnode)->vop = parent->vop;
    (*vnode)->parent = parent;
    (*vnode)->originMount = parent->originMount;
    if ((*vnode)->type == VNODE_TYPE_DIR) {
        (*vnode)->mode = S_IFDIR | PROCFS_DEFAULT_MODE;
    } else {
        (*vnode)->mode = S_IFREG | PROCFS_DEFAULT_MODE;
    }
    return ret;
}

int VfsProcfsRmdir(struct Vnode *parent, struct Vnode *vnode, const char *dirName)
{
    if (parent == NULL) {
        return -EINVAL;
    }

    struct ProcDirEntry *parentEntry = VnodeToEntry(parent);
    if ((parentEntry->procDirOps == NULL) || (parentEntry->procDirOps->rmdir == NULL)) {
        return -ENOSYS;
    }

    struct ProcDirEntry *dirEntry = VnodeToEntry(vnode);
    int ret = parentEntry->procDirOps->rmdir(parentEntry, dirEntry, dirName);
    if (ret < 0) {
        return ret;
    }
    vnode->data = NULL;
    return 0;
}
#endif

int VfsProcfsReaddir(struct Vnode *node, struct fs_dirent_s *dir)
{
    int result;
    char *buffer = NULL;
    unsigned int minSize, dstNameSize;
    struct ProcDirEntry *pde = NULL;
    int i = 0;

    if (dir == NULL) {
        return -EINVAL;
    }
    if (node->type != VNODE_TYPE_DIR) {
        return -ENOTDIR;
    }
    VnodeHold();
    pde = VnodeToEntry(node);
    if (pde == NULL) {
        VnodeDrop();
        return -EPERM;
    }

    while (i < dir->read_cnt) {
        buffer = (char *)zalloc(sizeof(char) * NAME_MAX);
        if (buffer == NULL) {
            VnodeDrop();
            PRINT_ERR("malloc failed\n");
            return -ENOMEM;
        }

        result = ReadProcFile(pde, (void *)buffer, NAME_MAX);
        if (result != ENOERR) {
            free(buffer);
            break;
        }
        dstNameSize = sizeof(dir->fd_dir[i].d_name);
        minSize = (dstNameSize < NAME_MAX) ? dstNameSize : NAME_MAX;
        result = strncpy_s(dir->fd_dir[i].d_name, dstNameSize, buffer, minSize);
        if (result != EOK) {
            VnodeDrop();
            free(buffer);
            return -ENAMETOOLONG;
        }
        dir->fd_dir[i].d_name[dstNameSize - 1] = '\0';
        dir->fd_position++;
        dir->fd_dir[i].d_off = dir->fd_position;
        dir->fd_dir[i].d_reclen = (uint16_t)sizeof(struct dirent);

        i++;
        free(buffer);
    }
    VnodeDrop();
    return i;
}

int VfsProcfsOpendir(struct Vnode *node,  struct fs_dirent_s *dir)
{
    VnodeHold();
    struct ProcDirEntry *pde = VnodeToEntry(node);
    if (pde == NULL) {
        VnodeDrop();
        return -EINVAL;
    }

    pde->pdirCurrent = pde->subdir;
    if (pde->pf == NULL) {
        VnodeDrop();
        return -EINVAL;
    }
    pde->pf->fPos = 0;
    VnodeDrop();
    return LOS_OK;
}

int VfsProcfsOpen(struct file *filep)
{
    if (filep == NULL) {
        return -EINVAL;
    }
    VnodeHold();
    struct Vnode *node = filep->f_vnode;
    struct ProcDirEntry *pde = VnodeToEntry(node);
    if (pde == NULL) {
        VnodeDrop();
        return -EPERM;
    }

    if (ProcOpen(pde->pf) != OK) {
        return -ENOMEM;
    }
    if (S_ISREG(pde->mode) && (pde->procFileOps != NULL) && (pde->procFileOps->open != NULL)) {
        (void)pde->procFileOps->open((struct Vnode *)pde, pde->pf);
    }
    if (S_ISDIR(pde->mode)) {
        pde->pdirCurrent = pde->subdir;
        pde->pf->fPos = 0;
    }
    filep->f_priv = (void *)pde;
    VnodeDrop();
    return LOS_OK;
}

int VfsProcfsClose(struct file *filep)
{
    int result = 0;
    if (filep == NULL) {
        return -EINVAL;
    }

    VnodeHold();
    struct Vnode *node = filep->f_vnode;
    struct ProcDirEntry *pde = VnodeToEntry(node);
    if ((pde == NULL) || (pde->pf == NULL)) {
        VnodeDrop();
        return -EPERM;
    }

    pde->pf->fPos = 0;
    if ((pde->procFileOps != NULL) && (pde->procFileOps->release != NULL)) {
        result = pde->procFileOps->release((struct Vnode *)pde, pde->pf);
    }
    LosBufRelease(pde->pf->sbuf);
    pde->pf->sbuf = NULL;
    VnodeDrop();
    return result;
}

int VfsProcfsStatfs(struct Mount *mnt, struct statfs *buf)
{
    (void)memset_s(buf, sizeof(struct statfs), 0, sizeof(struct statfs));
    buf->f_type = PROCFS_MAGIC;

    return LOS_OK;
}

int VfsProcfsClosedir(struct Vnode *vp, struct fs_dirent_s *dir)
{
    return LOS_OK;
}

ssize_t VfsProcfsReadlink(struct Vnode *vnode, char *buffer, size_t bufLen)
{
    int result = -EINVAL;
    if (vnode == NULL) {
        return result;
    }

    struct ProcDirEntry *pde = VnodeToEntry(vnode);
    if (pde == NULL) {
        return -EPERM;
    }

    if ((pde->procFileOps != NULL) && (pde->procFileOps->readLink != NULL)) {
        result = pde->procFileOps->readLink(pde, buffer, bufLen);
    }
    return result;
}

const struct MountOps procfs_operations = {
    .Mount = VfsProcfsMount,
    .Unmount = NULL,
    .Statfs = VfsProcfsStatfs,
};

static struct VnodeOps g_procfsVops = {
    .Lookup = VfsProcfsLookup,
    .Getattr = VfsProcfsStat,
    .Readdir = VfsProcfsReaddir,
    .Opendir = VfsProcfsOpendir,
    .Closedir = VfsProcfsClosedir,
    .Truncate = VfsProcfsTruncate,
    .Readlink = VfsProcfsReadlink,
#ifdef LOSCFG_KERNEL_PLIMITS
    .Mkdir = VfsProcfsMkdir,
    .Rmdir = VfsProcfsRmdir,
#endif
};

static struct file_operations_vfs g_procfsFops = {
    .read = VfsProcfsRead,
    .write = VfsProcfsWrite,
    .open = VfsProcfsOpen,
    .close = VfsProcfsClose
};

FSMAP_ENTRY(procfs_fsmap, "procfs", procfs_operations, FALSE, FALSE);
#endif
