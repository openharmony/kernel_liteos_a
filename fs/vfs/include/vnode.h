/*
 * Copyright (c) 2021-2021 Huawei Device Co., Ltd. All rights reserved.
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

#ifndef _VNODE_H_
#define _VNODE_H_

#include <sys/stat.h>
#include "fs/fs_operation.h"
#include "fs/file.h"
#include "los_list.h"

typedef LOS_DL_LIST LIST_HEAD;
typedef LOS_DL_LIST LIST_ENTRY;

#define VNODE_FLAG_MOUNT_NEW      (1 << 0) /* new mount vnode */
#define VNODE_FLAG_MOUNT_ORIGIN   (1 << 1) /* origin vnode */

#define V_CREATE     (1 << 0)
#define V_DUMMY      (1 << 2)

#ifndef VFS_ERROR
#define VFS_ERROR -1
#endif

#ifndef OK
#define OK 0
#endif

#define AT_REMOVEDIR 0x200

#define DEV_PATH_LEN 5

/* Permission flags */
#define READ_OP                 4
#define WRITE_OP                2
#define EXEC_OP                 1
#define UGO_NUMS                3
#define MODE_IXUGO              0111
#define USER_MODE_SHIFT         6
#define GROUP_MODE_SHIFT        3
#define UMASK_FULL              0777

/* Attribute flags. */
#define CHG_MODE 1
#define CHG_UID 2
#define CHG_GID 4
#define CHG_SIZE 8
#define CHG_ATIME 16
#define CHG_MTIME 32
#define CHG_CTIME 64

struct IATTR {
    /* This structure is used for record vnode attr. */
    unsigned int attr_chg_valid;
    unsigned int attr_chg_flags;
    unsigned attr_chg_mode;
    unsigned attr_chg_uid;
    unsigned attr_chg_gid;
    unsigned attr_chg_size;
    unsigned attr_chg_atime;
    unsigned attr_chg_mtime;
    unsigned attr_chg_ctime;
};

 /*
  * Vnode types.  VNODE_TYPE_UNKNOWN means no type.
  */
enum VnodeType {
    VNODE_TYPE_UNKNOWN,       /* unknown type */
    VNODE_TYPE_REG,           /* regular fle */
    VNODE_TYPE_DIR,           /* directory */
    VNODE_TYPE_BLK,           /* block device */
    VNODE_TYPE_CHR,           /* char device */
    VNODE_TYPE_BCHR,          /* block char mix device */
    VNODE_TYPE_FIFO,          /* pipe */
    VNODE_TYPE_LNK,           /* link */
};

struct fs_dirent_s;
struct VnodeOps;
struct IATTR;

struct Vnode {
    enum VnodeType type;                /* vnode type */
    int useCount;                       /* ref count of users */
    uint32_t hash;                      /* vnode hash */
    uint uid;                           /* uid for dac */
    uint gid;                           /* gid for dac */
    mode_t mode;                        /* mode for dac */
    LIST_HEAD parentPathCaches;         /* pathCaches point to parents */
    LIST_HEAD childPathCaches;          /* pathCaches point to children */
    struct Vnode *parent;               /* parent vnode */
    struct VnodeOps *vop;               /* vnode operations */
    struct file_operations_vfs *fop;    /* file operations */
    void *data;                         /* private data */
    uint32_t flag;                      /* vnode flag */
    LIST_ENTRY hashEntry;               /* list entry for bucket in hash table */
    LIST_ENTRY actFreeEntry;            /* vnode active/free list entry */
    struct Mount *originMount;          /* fs info about this vnode */
    struct Mount *newMount;             /* fs info about who mount on this vnode */
    char *filePath;                     /* file path of the vnode */
    struct page_mapping mapping;        /* page mapping of the vnode */
};

struct VnodeOps {
    int (*Create)(struct Vnode *parent, const char *name, int mode, struct Vnode **vnode);
    int (*Lookup)(struct Vnode *parent, const char *name, int len, struct Vnode **vnode);
    int (*Open)(struct Vnode *vnode, int fd, int mode, int flags);
    ssize_t (*ReadPage)(struct Vnode *vnode, char *buffer, off_t pos);
    ssize_t (*WritePage)(struct Vnode *vnode, char *buffer, off_t pos, size_t buflen);
    int (*Close)(struct Vnode *vnode);
    int (*Reclaim)(struct Vnode *vnode);
    int (*Unlink)(struct Vnode *parent, struct Vnode *vnode, const char *fileName);
    int (*Rmdir)(struct Vnode *parent, struct Vnode *vnode, const char *dirName);
    int (*Mkdir)(struct Vnode *parent, const char *dirName, mode_t mode, struct Vnode **vnode);
    int (*Readdir)(struct Vnode *vnode, struct fs_dirent_s *dir);
    int (*Opendir)(struct Vnode *vnode, struct fs_dirent_s *dir);
    int (*Rewinddir)(struct Vnode *vnode, struct fs_dirent_s *dir);
    int (*Closedir)(struct Vnode *vnode, struct fs_dirent_s *dir);
    int (*Getattr)(struct Vnode *vnode, struct stat *st);
    int (*Setattr)(struct Vnode *vnode, struct stat *st);
    int (*Chattr)(struct Vnode *vnode, struct IATTR *attr);
    int (*Rename)(struct Vnode *src, struct Vnode *dstParent, const char *srcName, const char *dstName);
    int (*Truncate)(struct Vnode *vnode, off_t len);
    int (*Truncate64)(struct Vnode *vnode, off64_t len);
    int (*Fscheck)(struct Vnode *vnode, struct fs_dirent_s *dir);
    int (*Link)(struct Vnode *src, struct Vnode *dstParent, struct Vnode **dst, const char *dstName);
    int (*Symlink)(struct Vnode *parentVnode, struct Vnode **newVnode, const char *path, const char *target);
    ssize_t (*Readlink)(struct Vnode *vnode, char *buffer, size_t bufLen);
};

typedef int VfsHashCmp(struct Vnode *vnode, void *arg);

int VnodesInit(void);
int VnodeDevInit(void);
int VnodeAlloc(struct VnodeOps *vop, struct Vnode **vnode);
int VnodeFree(struct Vnode *vnode);
int VnodeLookup(const char *path, struct Vnode **vnode, uint32_t flags);
int VnodeLookupFullpath(const char *fullpath, struct Vnode **vnode, uint32_t flags);
int VnodeLookupAt(const char *path, struct Vnode **vnode, uint32_t flags, struct Vnode *orgVnode);
int VnodeHold(void);
int VnodeDrop(void);
void VnodeRefDec(struct Vnode *vnode);
int VnodeFreeAll(const struct Mount *mnt);
int VnodeHashInit(void);
uint32_t VfsHashIndex(struct Vnode *vnode);
int VfsHashGet(const struct Mount *mount, uint32_t hash, struct Vnode **vnode, VfsHashCmp *fun, void *arg);
void VfsHashRemove(struct Vnode *vnode);
int VfsHashInsert(struct Vnode *vnode, uint32_t hash);
void ChangeRoot(struct Vnode *newRoot);
BOOL VnodeInUseIter(const struct Mount *mount);
struct Vnode *VnodeGetRoot(void);
void VnodeMemoryDump(void);
mode_t GetUmask(void);
int VfsPermissionCheck(uint fuid, uint fgid, mode_t fileMode, int accMode);
int VfsVnodePermissionCheck(const struct Vnode *node, int accMode);
LIST_HEAD* GetVnodeFreeList(void);
LIST_HEAD* GetVnodeActiveList(void);
LIST_HEAD* GetVnodeVirtualList(void);
int VnodeClearCache(void);

#endif /* !_VNODE_H_ */
