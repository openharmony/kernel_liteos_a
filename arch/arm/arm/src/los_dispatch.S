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

#include "asm.h"
#include "arch_config.h"

    .extern   OsSaveSignalContext
    .extern   OsSchedToUserReleaseLock
    .global   OsTaskSchedule
    .global   OsTaskContextLoad
    .global   OsIrqHandler

    .fpu vfpv4

/* macros to align and unalign the stack on 8 byte boundary for ABI compliance */
.macro STACK_ALIGN, reg
    MOV     \reg, sp
    TST     SP, #4
    SUBEQ   SP, #4
    PUSH    { \reg }
.endm

.macro STACK_RESTORE, reg
    POP     { \reg }
    MOV     sp, \reg
.endm

/* macros to save and restore fpu regs */
.macro PUSH_FPU_REGS reg1
#if !defined(LOSCFG_ARCH_FPU_DISABLE)
    VMRS    \reg1, FPEXC
    PUSH    {\reg1}
    VMRS    \reg1, FPSCR
    PUSH    {\reg1}
#if defined(LOSCFG_ARCH_FPU_VFP_D32)
    VPUSH   {D16-D31}
#endif
    VPUSH   {D0-D15}
#endif
.endm

.macro POP_FPU_REGS reg1
#if !defined(LOSCFG_ARCH_FPU_DISABLE)
    VPOP    {D0-D15}
#if defined(LOSCFG_ARCH_FPU_VFP_D32)
    VPOP    {D16-D31}
#endif
    POP     {\reg1}
    VMSR    FPSCR, \reg1
    POP     {\reg1}
    VMSR    FPEXC, \reg1
#endif
.endm

/*
 * R0: new task
 * R1: run task
 */
OsTaskSchedule:
    MRS      R2, CPSR
    STMFD    SP!, {R2}
    STMFD    SP!, {LR}
    STMFD    SP!, {LR}
    STMFD    SP!, {R12}

    /* jump R0 - R3 USP, ULR reserved */
    SUB      SP, SP, #(8 * 4)

    /* push R4 - R11*/
    STMFD    SP!, {R4-R11}

    /* save fpu registers */
    PUSH_FPU_REGS   R2

    /* store sp on running task */
    STR     SP, [R1]

OsTaskContextLoad:
    /* clear the flag of ldrex */
    CLREX

    /* switch to new task's sp */
    LDR     SP, [R0]

    /* restore fpu registers */
    POP_FPU_REGS    R2

    LDMFD   SP!, {R4-R11}
    LDR     R3, [SP, #(11 * 4)]
    AND     R0, R3, #CPSR_MASK_MODE
    CMP     R0, #CPSR_USER_MODE
    BNE     OsKernelTaskLoad

    MVN     R2, #CPSR_INT_DISABLE
    AND     R3, R3, R2
    STR     R3, [SP, #(11 * 4)]

#ifdef LOSCFG_KERNEL_SMP
    BL      OsSchedToUserReleaseLock
#endif

    /* jump sp, reserved */
    ADD     SP, SP, #(2 * 4)
    LDMFD   SP, {R13, R14}^
    ADD     SP, SP, #(2 * 4)
    LDMFD   SP!, {R0-R3, R12, LR}
    RFEIA   SP!

OsKernelTaskLoad:
    ADD     SP, SP, #(4 * 4)
    LDMFD   SP!, {R0-R3, R12, LR}
    RFEIA   SP!

OsIrqHandler:
    SUB     LR, LR, #4

    /* Save pc and cpsr to svc sp, ARMv6 and above support */
    SRSFD   #0x13!
    /* disable irq, switch to svc mode */
    CPSID   i, #0x13

    STMFD   SP!, {R0-R3, R12, LR}
    STMFD   SP, {R13, R14}^
    SUB     SP, SP, #(4 * 4)
    STR     R4, [SP, #0]

    /*
     * save fpu regs in case in case those been
     * altered in interrupt handlers.
     */
    PUSH_FPU_REGS   R0

    MOV     R4, SP
    EXC_SP_SET __svc_stack_top, OS_EXC_SVC_STACK_SIZE, R1, R2

    BLX     HalIrqHandler

    MOV     SP, R4

    /* process pending signals */
    BLX     OsTaskProcSignal
    BLX     OsSchedIrqEndCheckNeedSched

    /* restore fpu regs */
    POP_FPU_REGS R0
    LDR   R4, [SP, #0]

    /* Obtain the CPSR to determine the mode the system is in when the interrupt is triggered */
    LDR     R3, [SP, #(11 * 4)]
    AND     R1, R3, #CPSR_MASK_MODE
    CMP     R1, #CPSR_USER_MODE
    BNE     1f

    MOV     R0, SP
    STR     R7, [SP, #0]
    /* sp - sizeof(IrqContext) */
    SUB     SP, SP, #(12 * 4)
    MOV     R1, SP
    BLX     OsSaveSignalContext
    MOV     SP, R0
1:
    ADD     SP, SP, #(2 * 4)
    /* load user sp and lr, and jump cpsr */
    LDMFD   SP, {R13, R14}^
    ADD     SP, SP, #(2 * 4)
    LDMFD   SP!, {R0-R3, R12, LR}
    RFEIA   SP!

FUNCTION(ArchSpinLock)
    mov     r1, #1
1:
    ldrex   r2, [r0]
    cmp     r2, #0
    wfene
    strexeq r2, r1, [r0]
    cmpeq   r2, #0
    bne     1b
    dmb
    bx      lr

FUNCTION(ArchSpinTrylock)
    mov     r1, #1
    mov     r2, r0
    ldrex   r0, [r2]
    cmp     r0, #0
    strexeq r0, r1, [r2]
    dmb
    bx      lr

FUNCTION(ArchSpinUnlock)
    mov     r1, #0
    dmb
    str     r1, [r0]
    dsb
    sev
    bx      lr
