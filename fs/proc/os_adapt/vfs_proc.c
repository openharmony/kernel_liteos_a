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
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "internal.h"
#include "fs/dirent_fs.h"
#include "los_tables.h"
#include "proc_file.h"

#ifdef LOSCFG_FS_PROC

static char *ProcfsChangePath(const char *relpath, int namelen)
{
    char *path = (char *)relpath;

    if (*relpath != 0) {
        path = path - (namelen + 1); /* "/proc/xxx" need left avertence "/proc/" */
    } else {
        path = path - namelen;
    }

    return path;
}

int VfsProcfsOpen(struct file *filep, const char *relpath, int oflags, mode_t mode)
{
    struct ProcDirEntry *pde = NULL;
    if (filep == NULL) {
        return -EINVAL;
    }
    if ((unsigned int)oflags & O_DIRECTORY) {
        return -EACCES;
    }
    pde = OpenProcFile(filep->f_path, oflags);
    if (pde == NULL) {
        if ((unsigned int)oflags & O_CREAT) {
            return -EPERM;
        }
        return -ENOENT;
    }
    if (((unsigned int)oflags & O_CREAT) && ((unsigned int)oflags & O_EXCL)) {
        (void)CloseProcFile(pde);
        return -EEXIST;
    }
    if (S_ISDIR(pde->mode)) {
        (void)CloseProcFile(pde);
        return -EISDIR;
    }
    filep->f_priv = (void *)pde;
    filep->f_pos = 0;

    return 0;
}

int VfsProcfsClose(struct file *filep)
{
    if (filep == NULL) {
        return -EINVAL;
    }
    return CloseProcFile((struct ProcDirEntry *)filep->f_priv);
}

ssize_t VfsProcfsRead(struct file *filep, FAR char *buffer, size_t buflen)
{
    ssize_t size;
    struct ProcDirEntry *pde = NULL;
    if ((filep == NULL) || (buffer == NULL)) {
        return -EINVAL;
    }

    pde = (struct ProcDirEntry *)filep->f_priv;
    size = (ssize_t)ReadProcFile(pde, (void *)buffer, buflen);
    filep->f_pos = pde->pf->fPos;

    return size;
}

ssize_t VfsProcfsWrite(struct file *filep, const char *buffer, size_t buflen)
{
    ssize_t size;
    struct ProcDirEntry *pde = NULL;
    if ((filep == NULL) || (buffer == NULL)) {
        return -EINVAL;
    }

    pde = (struct ProcDirEntry *)filep->f_priv;
    size = (ssize_t)WriteProcFile(pde, (void *)buffer, buflen);
    filep->f_pos = pde->pf->fPos;

    return size;
}

off_t VfsProcfsLseek(struct file *filep, off_t offset, int whence)
{
    loff_t off;
    struct ProcDirEntry *pde = NULL;
    if (filep == NULL) {
        return -EINVAL;
    }

    pde = (struct ProcDirEntry *)filep->f_priv;
    if (pde == NULL) {
        return -EINVAL;
    }

    off = LseekProcFile(pde, (loff_t)offset, whence);
    filep->f_pos = pde->pf->fPos;

    return (off_t)off;
}

loff_t VfsProcfsLseek64(struct file *filep, loff_t offset, int whence)
{
    loff_t off;
    struct ProcDirEntry *pde = NULL;
    if (filep == NULL) {
        return -EINVAL;
    }

    pde = (struct ProcDirEntry *)filep->f_priv;
    if (pde == NULL) {
        return -EINVAL;
    }

    off = LseekProcFile(pde, offset, whence);
    filep->f_pos = pde->pf->fPos;

    return off;
}

int VfsProcfsIoctl(struct file *filep, int cmd, unsigned long arg)
{
    return -ENOSYS;
}

int VfsProcfsSync(struct file *filep)
{
    return -ENOSYS;
}

int VfsProcfsDup(const struct file *oldp, struct file *newp)
{
    return -ENOSYS;
}

int VfsProcfsOpenDir(struct inode *mountpt, const char *relpath, struct fs_dirent_s *dir)
{
    struct ProcDirEntry *pde = NULL;
    char *path = NULL;
    int oflags = O_APPEND;

    if (dir == NULL) {
        return -EINVAL;
    }

    path = ProcfsChangePath(relpath, PROCFS_MOUNT_POINT_SIZE);
    pde = OpenProcFile(path, oflags);
    if (pde == NULL) {
        return -ENOENT;
    }
    if (S_ISREG(pde->mode)) {
        (void)CloseProcFile(pde);
        return -ENOTDIR;
    }

    dir->u.fs_dir = (fs_dir_s)pde;

    return 0;
}

int VfsProcfsCloseDir(struct inode *mountpt, struct fs_dirent_s *dir)
{
    if (dir == NULL) {
        return -EINVAL;
    }

    return CloseProcFile((struct ProcDirEntry *)dir->u.fs_dir);
}

int VfsProcfsReadDir(struct inode *mountpt, struct fs_dirent_s *dir)
{
    int result;
    char *buffer = NULL;
    int buflen = MAX_NAMELEN;
    unsigned int min_size;
    unsigned int dst_name_size;
    struct ProcDirEntry *pde = NULL;
    int i = 0;
    if (dir == NULL) {
        return -EINVAL;
    }
    pde = (struct ProcDirEntry *)dir->u.fs_dir;

    buffer = (char *)malloc(sizeof(char) * MAX_NAMELEN);
    if (buffer == NULL) {
        PRINT_ERR("malloc failed\n");
        return -ENOMEM;
    }

    while (i < dir->read_cnt) {
        (void)memset_s(buffer, MAX_NAMELEN, 0, MAX_NAMELEN);

        result = ReadProcFile(pde, (void *)buffer, buflen);
        if (result != ENOERR) {
            break;
        }
        dst_name_size = sizeof(dir->fd_dir[i].d_name);
        min_size = (dst_name_size < MAX_NAMELEN) ? dst_name_size : MAX_NAMELEN;
        result = strncpy_s(dir->fd_dir[i].d_name, dst_name_size, buffer, min_size);
        if (result != EOK) {
            free(buffer);
            return -ENAMETOOLONG;
        }
        dir->fd_dir[i].d_name[dst_name_size - 1] = '\0';
        dir->fd_position++;
        dir->fd_dir[i].d_off = dir->fd_position;
        dir->fd_dir[i].d_reclen = (uint16_t)sizeof(struct dirent);
        i++;
    }

    free(buffer);
    return i;
}

int VfsProcfsRewinddir(struct inode *mountpt, struct fs_dirent_s *dir)
{
    int ret;
    off_t pos = 0;

    if (dir == NULL) {
        return -EINVAL;
    }

    ret = LseekDirProcFile((struct ProcDirEntry *)dir->u.fs_dir, &pos, SEEK_SET);
    if (ret != ENOERR) {
        return -ret;
    }

    return 0;
}

int VfsProcfsBind(struct inode *blkdriver, const void *data, FAR void **handle, const char *relpath)
{
    int len, length;

    if (relpath == NULL) {
        return -EINVAL;
    }

    len = strlen(relpath) + 1;
    length = strlen(PROCFS_MOUNT_POINT) + 1;
    if ((len == length) && !strncmp(relpath, PROCFS_MOUNT_POINT, length)) {
        spin_lock_init(&procfsLock);
        procfsInit = true;
        return ENOERR;
    }

    return -EPERM;
}

int VfsProcfsUnbind(void *handle, struct inode **blkdriver)
{
    return -EPERM;
}

int VfsProcfsStatfs(struct inode *mountpt, struct statfs *buf)
{
    if (buf == NULL) {
        return -EINVAL;
    }

    (void)memset_s(buf, sizeof(struct statfs), 0, sizeof(struct statfs));
    buf->f_type = PROCFS_MAGIC;

    return OK;
}

int VfsProcfsUnlink(struct inode *mountpt, const char *relpath)
{
    struct ProcDirEntry *pde = NULL;
    char *path = NULL;

    if (relpath == NULL) {
        return -EINVAL;
    }

    path = ProcfsChangePath(relpath, PROCFS_MOUNT_POINT_SIZE);
    pde = ProcFindEntry(path);
    if (pde == NULL) {
        return -ENOENT;
    }
    if (S_ISDIR(pde->mode)) {
        return -EISDIR;
    }

    return -EACCES;
}

int VfsProcfsMkdir(struct inode *mountpt, const char *relpath, mode_t mode)
{
    struct ProcDirEntry *pde = NULL;
    char *path = NULL;

    if (relpath == NULL) {
        return -EINVAL;
    }

    path = ProcfsChangePath(relpath, PROCFS_MOUNT_POINT_SIZE);
    pde = ProcFindEntry(path);
    if (pde == NULL) {
        return -ENOENT;
    }

    return -EEXIST;
}

int VfsProcfsRmdir(struct inode *mountpt, const char *relpath)
{
    struct ProcDirEntry *pde = NULL;
    char *path = NULL;

    if (relpath == NULL) {
        return -EINVAL;
    }

    path = ProcfsChangePath(relpath, PROCFS_MOUNT_POINT_SIZE);
    pde = ProcFindEntry(path);
    if (pde == NULL) {
        return -ENOENT;
    }

    return -EACCES;
}

int VfsProcfsRename(struct inode *mountpt, const char *oldrelpath, const char *newrelpath)
{
    return -ENOSYS;
}

int VfsProcfsStat(struct inode *mountpt, const char *relpath, struct stat *buf)
{
    int result;
    struct ProcStat statbuf;
    char *path = NULL;

    if ((relpath == NULL) || (buf == NULL)) {
        return -EINVAL;
    }

    path = ProcfsChangePath(relpath, PROCFS_MOUNT_POINT_SIZE);
    result = ProcStat(path, &statbuf);
    if (result != ENOERR) {
        return -result;
    }
    (void)memset_s(buf, sizeof(struct stat), 0, sizeof(struct stat));
    buf->st_mode = statbuf.stMode;

    return 0;
}

const struct mountpt_operations procfs_operations = {
    VfsProcfsOpen,        /* open */
    VfsProcfsClose,       /* close */
    VfsProcfsRead,        /* read */
    VfsProcfsWrite,       /* write */
    VfsProcfsLseek,       /* seek */
    VfsProcfsIoctl,       /* ioctl */
    NULL,                 /* mmap */
    VfsProcfsSync,        /* sync */
    VfsProcfsDup,         /* dup */
    NULL,                 /* fstat */
    NULL,                 /* truncate */
    VfsProcfsOpenDir,     /* opendir */
    VfsProcfsCloseDir,    /* closedir */
    VfsProcfsReadDir,     /* readdir */
    VfsProcfsRewinddir,   /* rewinddir */
    VfsProcfsBind,        /* bind */
    VfsProcfsUnbind,      /* unbind */
    VfsProcfsStatfs,      /* statfs */
    NULL,                 /* virstatfs */
    VfsProcfsUnlink,      /* unlink */
    VfsProcfsMkdir,       /* mkdir */
    VfsProcfsRmdir,       /* rmdir */
    VfsProcfsRename,      /* rename */
    VfsProcfsStat,        /* stat */
    NULL,                 /* for utime */
    NULL,                 /* chattr */
    VfsProcfsLseek64,     /* seek64 */
    NULL,                 /* getlabel */
    NULL,                 /* fallocate */
    NULL,                 /* fallocate64 */
    NULL,                 /* truncate64 */
    NULL,                 /* fscheck */
    NULL,                 /* map_pages */
    NULL,                 /* readpage */
    NULL,                 /* writepage */
};

FSMAP_ENTRY(procfs_fsmap, "procfs", procfs_operations, FALSE, FALSE);

#endif
