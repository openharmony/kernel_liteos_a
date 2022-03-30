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

#ifndef _LOS_ROOTFS_H
#define _LOS_ROOTFS_H

#include "los_typedef.h"

#define ROOT_DIR_NAME           "/"
#define STORAGE_DIR_NAME        "/storage"
#ifdef LOSCFG_STORAGE_EMMC
#define USERDATA_DIR_NAME       "/userdata"
#ifdef LOSCFG_PLATFORM_PATCHFS
#define PATCH_DIR_NAME          "/patch"
#endif
#endif
#define DEFAULT_MOUNT_DIR_MODE  0755
#define DEFAULT_MOUNT_DATA      NULL

#ifdef LOSCFG_STORAGE_SPINOR
#define FLASH_TYPE              "spinor"
#define ROOT_DEV_NAME           "/dev/spinorblk0"
#define USER_DEV_NAME           "/dev/spinorblk2"
#define ROOTFS_ADDR             0x600000
#define ROOTFS_SIZE             0x800000
#define USERFS_SIZE             0x80000
#elif defined (LOSCFG_STORAGE_SPINAND)
#define FLASH_TYPE              "nand"
#define ROOT_DEV_NAME           "/dev/nandblk0"
#define USER_DEV_NAME           "/dev/nandblk2"
#define ROOTFS_ADDR             0x600000
#define ROOTFS_SIZE             0x800000
#define USERFS_SIZE             0x80000
#elif defined (LOSCFG_PLATFORM_QEMU_ARM_VIRT_CA7)
#define ROOT_DEV_NAME           "/dev/cfiflash0"
#define USER_DEV_NAME           "/dev/cfiflash2"
#define ROOTFS_ADDR             CFIFLASH_ROOT_ADDR
#define ROOTFS_SIZE             0x1B00000
#define USERFS_SIZE             (CFIFLASH_CAPACITY - ROOTFS_ADDR - ROOTFS_SIZE)
#elif defined (LOSCFG_STORAGE_EMMC)
#if defined(LOSCFG_PLATFORM_STM32MP157)
#define ROOT_DEV_NAME           "/dev/mmcblk1p0"
#ifdef LOSCFG_PLATFORM_PATCHFS
#define PATCH_DEV_NAME          "/dev/mmcblk0p1"
#define USER_DEV_NAME           "/dev/mmcblk0p2"
#define USERDATA_DEV_NAME       "/dev/mmcblk0p3"
#else
#define USER_DEV_NAME           "/dev/mmcblk1p1"
#define USERDATA_DEV_NAME       "/dev/mmcblk1p2"
#endif
#else
#define ROOT_DEV_NAME           "/dev/mmcblk0p0"
#ifdef LOSCFG_PLATFORM_PATCHFS
#define PATCH_DEV_NAME          "/dev/mmcblk0p1"
#define USER_DEV_NAME           "/dev/mmcblk0p2"
#define USERDATA_DEV_NAME       "/dev/mmcblk0p3"
#else
#define USER_DEV_NAME           "/dev/mmcblk0p1"
#define USERDATA_DEV_NAME       "/dev/mmcblk0p2"
#endif
#endif
#define ROOTFS_ADDR             0xA00000
#define ROOTFS_SIZE             0x1400000
#define USERFS_SIZE             0x3200000
#ifdef LOSCFG_PLATFORM_PATCHFS
#define PATCH_SIZE              0x200000
#endif
#ifdef DEFAULT_MOUNT_DIR_MODE
#undef DEFAULT_MOUNT_DIR_MODE
#endif
#ifdef DEFAULT_MOUNT_DATA
#undef DEFAULT_MOUNT_DATA
#endif
#define DEFAULT_MOUNT_DIR_MODE  0777
#define DEFAULT_MOUNT_DATA      "umask=000"
#endif

INT32 OsMountRootfs(VOID);

#endif /* _LOS_ROOTFS_H */
