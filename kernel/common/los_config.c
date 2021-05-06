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

#include "los_config.h"
#include "string.h"
#include "stdio.h"
#include "los_oom.h"
#ifdef LOSCFG_COMPAT_LINUXKPI
#include "linux/workqueue.h"
#include "linux/module.h"
#endif
#include "los_sys.h"
#include "los_tick.h"
#include "los_task_pri.h"
#include "los_printf.h"
#include "los_swtmr.h"
#include "los_swtmr_pri.h"
#include "los_memory_pri.h"
#include "los_sem_pri.h"
#include "los_mux_pri.h"
#include "los_rwlock_pri.h"
#include "los_queue_pri.h"
#include "los_memstat_pri.h"
#include "los_hwi_pri.h"
#include "los_spinlock.h"
#include "los_mp.h"
#include "los_atomic.h"
#include "los_exc_pri.h"
#include "gic_common.h"
#include "los_vm_boot.h"

#ifdef LOSCFG_FS_VFS
#include "fs/fs.h"
#include "fs/fs_operation.h"
#endif

#if (LOSCFG_KERNEL_TRACE == YES)
#include "los_trace.h"
#endif

#ifdef LOSCFG_KERNEL_CPUP
#include "los_cpup_pri.h"
#endif

#ifdef LOSCFG_COMPAT_POSIX
#include "pprivate.h"
#endif

#ifdef LOSCFG_DRIVERS_HDF_PLATFORM_UART
#include "console.h"
#endif
#ifdef LOSCFG_ARCH_CORTEX_M7
#include "los_exc_pri.h"
#endif
#include "los_hw_tick_pri.h"
#include "los_hwi_pri.h"

#if defined(LOSCFG_HW_RANDOM_ENABLE) || defined (LOSCFG_DRIVERS_RANDOM)
#include "randomdev.h"
#include "yarrow.h"
#endif
#ifdef LOSCFG_SHELL_DMESG
#include "dmesg_pri.h"
#endif
#ifdef LOSCFG_SHELL_LK
#include "shell_pri.h"
#endif

#ifdef LOSCFG_KERNEL_PIPE
#include "pipe_common.h"
#endif

#include "los_process_pri.h"
#include "los_futex_pri.h"

#ifdef LOSCFG_KERNEL_VDSO
#include "los_vdso.h"
#endif

#if (LOSCFG_KERNEL_LITEIPC == YES)
#include "hm_liteipc.h"
#endif

#ifdef LOSCFG_DRIVERS_HIEVENT
#include "hievent_driver.h"
#endif

#if (LOSCFG_BASE_CORE_HILOG == YES)
#include "los_hilog.h"
#endif


STATIC SystemRebootFunc g_rebootHook = NULL;

VOID OsSetRebootHook(SystemRebootFunc func)
{
    g_rebootHook = func;
}

SystemRebootFunc OsGetRebootHook(VOID)
{
    return g_rebootHook;
}

extern UINT32 OsSystemInit(VOID);
extern VOID SystemInit(VOID);

LITE_OS_SEC_TEXT_INIT VOID osRegister(VOID)
{
    g_sysClock = OS_SYS_CLOCK;
    g_tickPerSecond =  LOSCFG_BASE_CORE_TICK_PER_SECOND;

    return;
}

LITE_OS_SEC_TEXT_INIT STATIC UINT32 OsIpcInit(VOID)
{
    UINT32 ret;
#if (LOSCFG_BASE_IPC_SEM == YES)
    ret = OsSemInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

#if (LOSCFG_BASE_IPC_QUEUE == YES)
    ret = OsQueueInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif
    return LOS_OK;
}

#ifdef LOSCFG_KERNEL_PIPE
LITE_OS_SEC_TEXT_INIT STATIC VOID OsDriverPipeInit(VOID)
{
    (VOID)pipe_init();
}
#endif

#ifdef LOSCFG_DRIVERS_HIEVENT
LITE_OS_SEC_TEXT_INIT STATIC VOID OsDriverHiEventInit(VOID)
{
    (VOID)HieventInit();
}
#endif

#ifdef LOSCFG_COMPAT_BSD
extern void configure (void);
LITE_OS_SEC_TEXT_INIT STATIC INT32 OsBsdInit(VOID)
{
    /*
     * WORKAROUND: Inside configure(), nexus_init() function calls
     *             HiSi-specific, library procedure - machine_resource_init().
     *             The latter one is defined in libhi35xx_bsp.a which is only
     *             available for Hi3516 and Hi3518.
     *             Temporarily ifdef configure until this routine is implemented
     *             by other platforms.
     */
#if defined(LOSCFG_PLATFORM_HI3516DV300) || defined(LOSCFG_PLATFORM_HI3518EV300)
    configure();
#endif
    mi_startup(SI_SUB_ARCH_INIT);
    return LOS_OK;
}
#endif

LITE_OS_SEC_TEXT_INIT INT32 OsMain(VOID)
{
    UINT32 ret;

    osRegister();

#ifdef LOSCFG_SHELL_DMESG
    ret = OsDmesgInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

#ifdef LOSCFG_SHELL_LK
    OsLkLoggerInit(NULL);
#endif

#if (LOSCFG_KERNEL_TRACE == YES)
    LOS_TraceInit();
#endif

#ifdef LOSCFG_EXC_INTERACTION
#ifdef LOSCFG_ARCH_CORTEX_M7
    /* 4096: 4K space for Stack */
    ret = OsMemExcInteractionInit((UINT32)&__bss_end + 4096);
#else
    ret = OsMemExcInteractionInit((UINTPTR)&__bss_end);
#endif
    if (ret != LOS_OK) {
        return ret;
    }
#endif

#if (LOSCFG_PLATFORM_HWI == YES)
    OsHwiInit();
#endif

    OsExcInit();

    ret = OsTickInit(g_sysClock, LOSCFG_BASE_CORE_TICK_PER_SECOND);
    if (ret != LOS_OK) {
        return ret;
    }

#ifdef LOSCFG_PLATFORM_UART_WITHOUT_VFS
#ifdef LOSCFG_DRIVERS
    uart_init();
#endif
#ifdef LOSCFG_SHELL
#endif //LOSCFG_SHELL
#endif //LOSCFG_PLATFORM_UART_WITHOUT_VFS
    ret = OsTaskInit();
    if (ret != LOS_OK) {
        PRINT_ERR("OsTaskInit error\n");
        return ret;
    }

#if ((LOSCFG_BASE_IPC_QUEUE == YES) || (LOSCFG_BASE_IPC_MUX == YES) || \
     (LOSCFG_BASE_IPC_SEM == YES) || (LOSCFG_BASE_IPC_RWLOCK == YES))
    ret = OsIpcInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

    ret = OsSysMemInit();
    if (ret != LOS_OK) {
        PRINT_ERR("OsSysMemInit error\n");
        return ret;
    }

#ifdef LOSCFG_KERNEL_SYSCALL
    SyscallHandleInit();
#endif

    /*
     * CPUP should be inited before first task creation which depends on the semaphore
     * when LOSCFG_KERNEL_SMP_TASK_SYNC is enabled. So don't change this init sequence
     * if not neccessary. The sequence should be like this:
     * 1. OsIpcInit
     * 2. OsCpupInit
     * 3. other inits have task creation
     */
#ifdef LOSCFG_KERNEL_CPUP
    ret = OsCpupInit();
    if (ret != LOS_OK) {
        PRINT_ERR("OsCpupInit error\n");
        return ret;
    }
#endif

    ret = OsSystemProcessCreate();
    if (ret != LOS_OK) {
        return ret;
    }

#if (LOSCFG_BASE_CORE_SWTMR == YES)
    ret = OsSwtmrInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

#ifdef LOSCFG_KERNEL_CPUP
    OsCpupGuardCreator();
#endif

#if (LOSCFG_KERNEL_SMP == YES)
    (VOID)OsMpInit();
#endif

#if defined(LOSCFG_HW_RANDOM_ENABLE) || defined (LOSCFG_DRIVERS_RANDOM)
    random_alg_context.ra_init_alg(NULL);
    run_harvester_iterate(NULL);
#endif

#ifdef LOSCFG_COMPAT_BSD
    ret = OsBsdInit();
    if (ret != LOS_OK) {
        PRINT_ERR("init bsd failed!\n");
        return ret;
    }
#endif

#ifdef LOSCFG_KERNEL_PIPE
    OsDriverPipeInit();
#endif

    ret = OsSystemInit();
    if (ret != LOS_OK) {
        return ret;
    }
#if (LOSCFG_BASE_CORE_HILOG == YES)
    ret = HiLogDriverInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

#if LOSCFG_DRIVERS_HIEVENT
    OsDriverHiEventInit();
#endif

#if (LOSCFG_KERNEL_LITEIPC == YES)
    ret = LiteIpcInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

#ifdef LOSCFG_KERNEL_VDSO
    ret = OsInitVdso();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

#ifdef LOSCFG_KERNEL_VM
    ret = OsFutexInit();
    if (ret != LOS_OK) {
        PRINT_ERR("Create futex failed : %d!\n", ret);
        return ret;
    }

    ret = OomTaskInit();
    if (ret != LOS_OK) {
        return ret;
    }
#endif

    return LOS_OK;
}

STATIC UINT32 OsSystemInitTaskCreate(VOID)
{
    UINT32 taskID;
    TSK_INIT_PARAM_S sysTask;

    (VOID)memset_s(&sysTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    sysTask.pfnTaskEntry = (TSK_ENTRY_FUNC)SystemInit;
    sysTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    sysTask.pcName = "SystemInit";
    sysTask.usTaskPrio = LOSCFG_BASE_CORE_TSK_DEFAULT_PRIO;
    sysTask.uwResved = LOS_TASK_STATUS_DETACHED;
#if (LOSCFG_KERNEL_SMP == YES)
    sysTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif
    return LOS_TaskCreate(&taskID, &sysTask);
}


UINT32 OsSystemInit(VOID)
{
    UINT32 ret;
#ifdef LOSCFG_FS_VFS
    los_vfs_init();
#endif
#ifdef LOSCFG_COMPAT_LINUXKPI
    g_pstSystemWq = create_workqueue("system_wq");
#endif
    ret = OsSystemInitTaskCreate();
    if (ret != LOS_OK) {
        return ret;
    }

    return 0;
}

