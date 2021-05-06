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

    .extern   g_losTask
    .extern   g_intCount
    .extern   g_curNestCount
    .extern   OsExcHandleEntry
    .extern   __svc_stack_top
    .extern   __exc_stack_top
    .extern   __stack_chk_guard
    .extern   OsRandomStackGuard
#ifdef LOSCFG_GDB
    .extern   OsUndefIncExcHandleEntry
#if __LINUX_ARM_ARCH__ >= 7
    .extern   OsPrefetchAbortExcHandleEntry
    .extern   OsDataAbortExcHandleEntry
#endif
#endif
    .extern OsSaveSignalContext
    .extern OsRestorSignalContext
    .extern OsArmSharedPageFault
    .extern OsArmA32SyscallHandle

    .global   _osExceptFiqHdl
    .global   _osExceptAddrAbortHdl
    .global   _osExceptDataAbortHdl
    .global   _osExceptPrefetchAbortHdl
    .global   _osExceptSwiHdl
    .global   _osExceptUndefInstrHdl
#if defined(LOSCFG_CC_STACKPROTECTOR_ALL) || \
    defined(LOSCFG_CC_STACKPROTECTOR_STRONG) || \
    defined(LOSCFG_CC_STACKPROTECTOR)
    .global   __stack_chk_guard_setup
#endif

    .fpu vfpv4

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

#ifdef LOSCFG_GDB
.macro GDB_HANDLE fun
    SUB     SP, SP, #12

    STMFD   SP!, {R0-R12}
    MRS     R1, SPSR
    STMFD   SP!, {R1}  @save spsr

    ADD     R0, SP, #14 * 4
    MOV     R3, LR  @save pc

    MRS     R1, CPSR
    MRS     R2, SPSR
    MOV     R4, SP

    ORR     R2, R2, #(CPSR_INT_DISABLE)
    MSR     CPSR_c, R2

    STR     SP, [R0]  @SP
    STR     LR, [R0, #4]  @LR
    STR     R3, [R0, #8]  @PC

    ORR     R1, R1, #(CPSR_INT_DISABLE)
    BIC     R1, R1, #OS_PSR_THUMB
    MSR     CPSR_c, R1
    MOV     R0, R4

    BL \fun

    ADD     SP, SP, #4
    LDMFD   SP!, {R0-R12}

    MOV     R0, SP
    ADD     SP, SP, #8

    LDR     R1, [R0, #8]  @get pc
    STMFD   SP!, {R1}

    AND     R1, R1, #0x03
    CMP     R1, #0
    BEQ     1f
    LDR     R1, [R0, #-14 * 4]
    ORR     R1, R1, #OS_PSR_THUMB
    B       2f
1:
    LDR     R1, [R0, #-14 * 4]

2:
    MSR     SPSR, R1

    LDR     R1, [R0, #-12 * 4]  @get R1
    STMFD   SP!, {R1}
    LDR     R1, [R0,#-13 * 4]  @get R0
    STMFD   SP!, {R1}

    LDMFD   SP!, {R0-R1, PC}^
.endm
#endif

#if defined(LOSCFG_CC_STACKPROTECTOR_ALL) || \
    defined(LOSCFG_CC_STACKPROTECTOR_STRONG) || \
    defined(LOSCFG_CC_STACKPROTECTOR)
@ Description: Stack-Protector Init
__stack_chk_guard_setup:
    PUSH    {FP, LR}
#if defined(LOSCFG_PLATFORM_HI3516DV300) || defined(LOSCFG_PLATFORM_HI3518EV300)
    /* Randomize __stack_chk_quard using platform-specific function. */
    BL      OsRandomStackGuard
    LDR     R1, =__stack_chk_guard
    MOV     R3, R0
    ORR     R2, R3, #0X80000000
    STR     R2, [R1]
#endif
    POP     {FP, PC}
#endif

@ Description: Undefined instruction exception handler
_osExceptUndefInstrHdl:
#ifdef LOSCFG_GDB
    GDB_HANDLE OsUndefIncExcHandleEntry
#else
    SRSFD   #0x13!
    CPSID   i, #0x13
    STMFD   SP!, {R0-R3, R12, LR}
    SUB     SP, SP, #(4 * 2)
    MOV     R2, #0
    MOV     R3, #0
    STMFD   SP!, {R2-R3}

    MOV     R0, #OS_EXCEPT_UNDEF_INSTR                        @ Set exception ID to OS_EXCEPT_UNDEF_INSTR.
    B       _osExceptDispatch                                 @ Branch to global exception handler.

#endif

@ Description: Software interrupt exception handler
_osExceptSwiHdl:
    SRSFD   #0x13!
    STMFD   SP!, {R0-R3, R12, LR}
    STMFD   SP, {R13, R14}^
    SUB     SP, SP, #(4 * 4)
    STMIA   SP, {R7}

#ifdef LOSCFG_KERNEL_SYSCALL
    LDR     R3, [SP, #(11 * 4)]
    AND     R1, R3, #CPSR_MASK_MODE                          @ Interrupted mode
    CMP     R1, #CPSR_USER_MODE                              @ User mode
    BNE     _osKernelSVCHandler                               @ Branch if not user mode

    CMP     R7, #119                                         @ __NR_sigreturn
    BNE     _osIsSyscall
    MOV     R0, SP
    BLX     OsRestorSignalContext
    MOV     SP, R0
    B       _osSyscallReturn

_osIsSyscall:
    STMFD   SP!, {R4-R11}

    PUSH_FPU_REGS R1

    MOV     R0, SP
    MOV     FP, #0                                           @ Init frame pointer
    CPSIE   I
    BLX     OsArmA32SyscallHandle
    CPSID   I

    POP_FPU_REGS R1
    LDMFD   SP!, {R4-R11}

    LDR     R3, [SP, #(11 * 4)]
    AND     R1, R3, #CPSR_MASK_MODE
    CMP     R1, #CPSR_USER_MODE
    BNE     _osSyscallReturn

    MOV     R0, SP
    BLX     OsSaveSignalContext
    MOV     SP, R0

_osSyscallReturn:
    LDMFD   SP, {R7}
    ADD     SP, SP, #(2 * 4)
    LDMFD   SP, {R13, R14}^                                  @ Restore user mode R13/R14
    ADD     SP, SP, #(2 * 4)
    LDMFD   SP!, {R0-R3, R12, LR}
    RFEIA   SP!                                              @ Return to user

_osKernelSVCHandler:
#endif
    MOV     R0, #0
    STR     R0, [SP, #0]
    STR     R0, [SP, #4]
    MOV     R0, #OS_EXCEPT_SWI                               @ Set exception ID to OS_EXCEPT_SWI.
    B       _osExceptDispatch                                @ Branch to global exception handler.

@ Description: Prefectch abort exception handler
_osExceptPrefetchAbortHdl:
#ifdef LOSCFG_GDB
#if __LINUX_ARM_ARCH__ >= 7
    GDB_HANDLE OsPrefetchAbortExcHandleEntry
#endif
#else
    SUB     LR, LR, #4                                       @ LR offset to return from this exception: -4.

    SRSFD   #0x13!
    CPSID   i, #0x13
    STMFD   SP!, {R0-R3, R12, LR}
    STMFD   SP, {R13, R14}^
    SUB     SP, SP, #(2 * 4)

    MRC     P15, 0, R2, C6, C0, 2
    MRC     P15, 0, R3, C5, C0, 1
    STMFD   SP!, {R2-R3}

#ifdef LOSCFG_KERNEL_VM
    LDR     R0, [SP, #(11 * 4)]
    AND     R0, R0, #CPSR_MASK_MODE                          @ Interrupted mode
    CMP     R0, #CPSR_USER_MODE                              @ User mode
    BNE     _osKernelExceptPrefetchAbortHdl

    MOV    R1, SP
    PUSH_FPU_REGS R0

    MOV    R0, #OS_EXCEPT_PREFETCH_ABORT
    CPSIE  I
    BLX    OsArmSharedPageFault
    CPSID  I
    CMP    R0, #0

    POP_FPU_REGS R0
    BEQ   _osExcPageFaultReturn
#endif

_osKernelExceptPrefetchAbortHdl:
    MOV    R0, #OS_EXCEPT_PREFETCH_ABORT
    B       _osExceptDispatch                                @ Branch to global exception handler.
#endif

@ Description: Data abort exception handler
_osExceptDataAbortHdl:
#ifdef LOSCFG_GDB
#if __LINUX_ARM_ARCH__ >= 7
    GDB_HANDLE OsDataAbortExcHandleEntry
#endif
#else
    SUB     LR, LR, #8                                       @ LR offset to return from this exception: -8.

    SRSFD   #0x13!
    CPSID   i, #0x13
    STMFD   SP!, {R0-R3, R12, LR}
    STMFD   SP, {R13, R14}^
    SUB     SP, SP, #(2 * 4)

    MRC     P15, 0, R2, C6, C0, 0
    MRC     P15, 0, R3, C5, C0, 0
    STMFD   SP!, {R2-R3}

#ifdef LOSCFG_KERNEL_VM
    MOV    R1, SP
    PUSH_FPU_REGS R0

    MOV     R0, #OS_EXCEPT_DATA_ABORT                        @ Set exception ID to OS_EXCEPT_DATA_ABORT.
    CPSIE  I
    BLX    OsArmSharedPageFault
    CPSID  I
    CMP    R0, #0
    POP_FPU_REGS R0
    BEQ   _osExcPageFaultReturn
#endif

    MOV   R0, #OS_EXCEPT_DATA_ABORT
    B     _osExceptDispatch
#endif

#ifdef LOSCFG_KERNEL_VM
_osExcPageFaultReturn:
    ADD    SP, SP, #(2 * 4)
    LDMFD  SP, {R13, R14}^
    ADD    SP, SP, #(2 * 4)
    LDMFD  SP!, {R0-R3, R12, LR}
    RFEIA  SP!
#endif

@ Description: Address abort exception handler
_osExceptAddrAbortHdl:
    SUB     LR, LR, #8                                       @ LR offset to return from this exception: -8.

    SRSFD   #0x13!
    CPSID   i, #0x13
    STMFD   SP!, {R0-R3, R12, LR}
    STMFD   SP, {R13, R14}^
    SUB     SP, SP, #(2 * 4)

    MOV     R2, #0
    MOV     R3, #0
    STMFD   SP!, {R2-R3}

    MOV     R0, #OS_EXCEPT_ADDR_ABORT                        @ Set exception ID to OS_EXCEPT_ADDR_ABORT.
    B       _osExceptDispatch                                @ Branch to global exception handler.

@ Description: Fast interrupt request exception handler
_osExceptFiqHdl:
    SUB     LR, LR, #4                                       @ LR offset to return from this exception: -4.

    SRSFD   #0x13!
    CPSID   i, #0x13
    STMFD   SP!, {R0-R3, R12, LR}
    STMFD   SP, {R13, R14}^
    SUB     SP, SP, #(2 * 4)

    MOV     R2, #0
    MOV     R3, #0
    STMFD   SP!, {R2-R3}

@ Description: Exception handler
@ Parameter  : R0     Exception Type
@ Regs Hold  : R3     Exception`s CPSR
_osExceptDispatch:
    STMFD   SP!, {R4-R11}
    LDR     R8, [SP, #(4 * 8)]
    LDR     R9, [SP, #(4 * 9)]
    MOV     R1, SP

    EXC_SP_SET __exc_stack_top, OS_EXC_STACK_SIZE, R6, R7

    MRC     P15, 0, R4, C0, C0, 5
    AND     R4, R4, #MPIDR_CPUID_MASK                         @ Get Current cpu id
    LSL     R2, R4, #2
    LDR     R3, =g_curNestCount                               @ if(g_curNestCount > 0) dump to _osExceptionGetSP
    ADD     R3, R3, R2
    LDR     R4, [R3]

    CMP     R4, #0
    BNE     _osExceptionGetSP

    LDR     R3, =g_intCount                                   @ Judge the exception is occur in task stack or system stack
    ADD     R3, R3, R2
    LDR     R4, [R3]

    CMP     R4, #0                                            @ if (g_intCount[ArchCurrCpuid()] > 0)
    BNE     _osExceptionGetSP                                 @ can not switch svc stack

    EXC_SP_SET __svc_stack_top, OS_EXC_SVC_STACK_SIZE, R6, R7 @ Switch to unified exception stack.
    ADD     R4, R4, #1
    STR     R4, [R3]

_osExceptionGetSP:
    MOV     R2, R8                                            @ far
    MOV     R3, R9                                            @ fsr
    LDR     R5, =OsExcHandleEntry                             @ OsExcHandleEntry(UINT32 excType, ExcContext * excBufAddr)
    BX      R5

    .end
