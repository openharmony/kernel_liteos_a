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

#include "los_hw_pri.h"
#include "los_task_pri.h"

/* support cpu vendors */
CpuVendor g_cpuTable[] = {
    /* armv7-a */
    { 0xc07, "Cortex-A7" },
    { 0xc09, "Cortex-A9" },
    { 0, NULL }
};

/* logical cpu mapping */
UINT64 g_cpuMap[LOSCFG_KERNEL_CORE_NUM] = {
    [0 ... LOSCFG_KERNEL_CORE_NUM - 1] = (UINT64)(-1)
};

/* bit[30] is enable FPU */
#define FP_EN (1U << 30)
LITE_OS_SEC_TEXT_INIT VOID OsTaskExit(VOID)
{
    __asm__ __volatile__("swi  0");
}

#ifdef LOSCFG_GDB
STATIC VOID OsTaskEntrySetupLoopFrame(UINT32) __attribute__((noinline, naked));
VOID OsTaskEntrySetupLoopFrame(UINT32 arg0)
{
    asm volatile("\tsub fp, sp, #0x4\n"
                 "\tpush {fp, lr}\n"
                 "\tadd fp, sp, #0x4\n"
                 "\tpush {fp, lr}\n"

                 "\tadd fp, sp, #0x4\n"
                 "\tbl OsTaskEntry\n"

                 "\tpop {fp, lr}\n"
                 "\tpop {fp, pc}\n");
}
#endif

LITE_OS_SEC_TEXT_INIT VOID *OsTaskStackInit(UINT32 taskID, UINT32 stackSize, VOID *topStack, BOOL initFlag)
{
    if (initFlag == TRUE) {
        OsStackInit(topStack, stackSize);
    }
    TaskContext *taskContext = (TaskContext *)(((UINTPTR)topStack + stackSize) - sizeof(TaskContext));

    /* initialize the task context */
#ifdef LOSCFG_GDB
    taskContext->PC = (UINTPTR)OsTaskEntrySetupLoopFrame;
#else
    taskContext->PC = (UINTPTR)OsTaskEntry;
#endif
    taskContext->LR = (UINTPTR)OsTaskExit;  /* LR should be kept, to distinguish it's THUMB or ARM instruction */
    taskContext->R0 = taskID;               /* R0 */

#ifdef LOSCFG_THUMB
    taskContext->regCPSR = PSR_MODE_SVC_THUMB; /* CPSR (Enable IRQ and FIQ interrupts, THUMNB-mode) */
#else
    taskContext->regCPSR = PSR_MODE_SVC_ARM;   /* CPSR (Enable IRQ and FIQ interrupts, ARM-mode) */
#endif

#if !defined(LOSCFG_ARCH_FPU_DISABLE)
    /* 0xAAA0000000000000LL : float reg initialed magic word */
    for (UINT32 index = 0; index < FP_REGS_NUM; index++) {
        taskContext->D[index] = 0xAAA0000000000000LL + index; /* D0 - D31 */
    }
    taskContext->regFPSCR = 0;
    taskContext->regFPEXC = FP_EN;
#endif

    return (VOID *)taskContext;
}

LITE_OS_SEC_TEXT VOID OsUserCloneParentStack(VOID *childStack, UINTPTR parentTopOfStack, UINT32 parentStackSize)
{
    LosTaskCB *task = OsCurrTaskGet();
    sig_cb *sigcb = &task->sig;
    VOID *cloneStack = NULL;

    if (sigcb->sigContext != NULL) {
        cloneStack = (VOID *)((UINTPTR)sigcb->sigContext - sizeof(TaskContext));
    } else {
        cloneStack = (VOID *)(((UINTPTR)parentTopOfStack + parentStackSize) - sizeof(TaskContext));
    }

    (VOID)memcpy_s(childStack, sizeof(TaskContext), cloneStack, sizeof(TaskContext));
    ((TaskContext *)childStack)->R0 = 0;
}

LITE_OS_SEC_TEXT_INIT VOID OsUserTaskStackInit(TaskContext *context, UINTPTR taskEntry, UINTPTR stack)
{
    LOS_ASSERT(context != NULL);

#ifdef LOSCFG_THUMB
    context->regCPSR = PSR_MODE_USR_THUMB;
#else
    context->regCPSR = PSR_MODE_USR_ARM;
#endif
    context->R0 = stack;
    context->USP = TRUNCATE(stack, LOSCFG_STACK_POINT_ALIGN_SIZE);
    context->ULR = 0;
    context->PC = (UINTPTR)taskEntry;
}

VOID OsInitSignalContext(const VOID *sp, VOID *signalContext, UINTPTR sigHandler, UINT32 signo, UINT32 param)
{
    IrqContext *newSp = (IrqContext *)signalContext;
    (VOID)memcpy_s(signalContext, sizeof(IrqContext), sp, sizeof(IrqContext));
    newSp->PC = sigHandler;
    newSp->R0 = signo;
    newSp->R1 = param;
}

DEPRECATED VOID Dmb(VOID)
{
    __asm__ __volatile__ ("dmb" : : : "memory");
}

DEPRECATED VOID Dsb(VOID)
{
    __asm__ __volatile__("dsb" : : : "memory");
}

DEPRECATED VOID Isb(VOID)
{
    __asm__ __volatile__("isb" : : : "memory");
}

VOID FlushICache(VOID)
{
    /*
     * Use ICIALLUIS instead of ICIALLU. ICIALLUIS operates on all processors in the Inner
     * shareable domain of the processor that performs the operation.
     */
    __asm__ __volatile__ ("mcr p15, 0, %0, c7, c1, 0" : : "r" (0) : "memory");
}

VOID DCacheFlushRange(UINT32 start, UINT32 end)
{
    arm_clean_cache_range(start, end);
}

VOID DCacheInvRange(UINT32 start, UINT32 end)
{
    arm_inv_cache_range(start, end);
}

