/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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

#include "It_los_queue.h"
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cpluscplus */
#endif /* __cpluscplus */


static VOID TaskF01(VOID)
{
    UINT32 ret;
    CHAR buff1[QUEUE_SHORT_BUFFER_LENGTH] = "UniDSP";
    CHAR buff2[QUEUE_SHORT_BUFFER_LENGTH] = "";
    UINT32 readSize;

    TEST_HwiClear(HWI_NUM_TEST);

    g_testCount++;

    ret = LOS_QueueWriteCopy(g_testQueueID01, &buff1, QUEUE_SHORT_BUFFER_LENGTH, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    readSize = QUEUE_SHORT_BUFFER_LENGTH;
    ret = LOS_QueueReadCopy(g_testQueueID01, &buff2, &readSize, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    g_testCount++;

    LOS_TaskDelete(g_testTaskID01);

    return;
EXIT:
    g_testCount = 0;
    LOS_TaskDelete(g_testTaskID01);
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    CHAR buff1[QUEUE_SHORT_BUFFER_LENGTH] = "UniDSP";
    CHAR buff2[QUEUE_SHORT_BUFFER_LENGTH] = "";
    QUEUE_INFO_S queueInfo;
    TSK_INIT_PARAM_S task1 = { 0 };
    UINT32 readSize;

    task1.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF01;
    task1.pcName = "queue_109";
    task1.uwStackSize = TASK_STACK_SIZE_TEST;
    task1.usTaskPrio = 23; // 23, Set task priority.

    g_testCount = 0;

    ret = LOS_QueueCreate("Q1", 3, &g_testQueueID01, 0, QUEUE_SHORT_BUFFER_LENGTH); // 3, Set the queue length.
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    TestExtraTaskDelay(TEST_TASKDELAY_2TICK);

    ICUNIT_GOTO_EQUAL(g_testCount, 2, g_testCount, EXIT); // 2, Here, assert that g_testCount is equal to 2.

    g_testCount++;

    readSize = QUEUE_SHORT_BUFFER_LENGTH;
    ret = LOS_QueueReadCopy(g_testQueueID01, &buff2, &readSize, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_QUEUE_ISEMPTY, ret, EXIT);

    ret = LOS_QueueInfoGet(g_testQueueID01, &queueInfo);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(queueInfo.usQueueLen, 3, queueInfo.usQueueLen, EXIT); // 3, Here, assert that g_testCount is equal to 3.
    ICUNIT_GOTO_EQUAL(queueInfo.uwQueueID, g_testQueueID01, queueInfo.uwQueueID, EXIT);

    ret = LOS_QueueDelete(g_testQueueID01);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_GOTO_EQUAL(g_testCount, 3, g_testCount, EXIT); // 3, Here, assert that g_testCount is equal to 3.

    return LOS_OK;

EXIT:
    LOS_TaskDelete(g_testTaskID01);
    LOS_QueueDelete(g_testQueueID01);
    return LOS_OK;
}

VOID ItLosQueue112(VOID)
{
    TEST_ADD_CASE("ItLosQueue112", Testcase, TEST_LOS, TEST_QUE, TEST_LEVEL2, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */
