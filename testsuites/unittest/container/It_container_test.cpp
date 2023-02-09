/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
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
#include <climits>
#include <gtest/gtest.h>
#include <cstdio>
#include "It_container_test.h"

const char *USERDATA_DIR_NAME = "/userdata";
const char *ACCESS_FILE_NAME  = "/userdata/mntcontainertest";
const char *MNT_ACCESS_FILE_NAME  = "/mntcontainertest";
const char *USERDATA_DEV_NAME = "/dev/mmcblk0p2";
const char *FS_TYPE           = "vfat";

const int BIT_ON_RETURN_VALUE  = 8;
const int STACK_SIZE           = 1024 * 1024;
const int CHILD_FUNC_ARG       = 0x2088;

int ChildFunction(void *args)
{
    (void)args;
    const int sleep_time = 2;
    sleep(sleep_time);
    return 0;
}

pid_t CloneWrapper(int (*func)(void *), int flag, void *args)
{
    pid_t pid;
    char *stack = (char *)mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                               -1, 0);
    if (stack == MAP_FAILED) {
        return -1;
    }
    char *stackTop = stack + STACK_SIZE;

    pid = clone(func, stackTop, flag, args);
    munmap(stack, STACK_SIZE);
    return pid;
}

int WaitChild(pid_t pid, int *status, int errNo1, int errNo2)
{
    int ret = waitpid(pid, status, 0);
    if (ret != pid) {
        printf("[ERR] WaitChild pid=%d return pid=%d\n", pid, ret);
        return errNo1;
    }
    if (status == nullptr) {
        return 0;
    }
    ret = WIFEXITED(*status);
    if (ret == 0) {
        printf("[ERR] WaitChild pid=%d WIFEXITED(status)=%d\n", pid, WIFEXITED(*status));
        return errNo2;
    }
    ret = WEXITSTATUS(*status);
    if (ret != 0) {
        printf("[ERR] WaitChild pid=%d WEXITSTATUS(status)=%d\n", pid, WEXITSTATUS(*status));
        return errNo2;
    }
    return 0;
}

std::string GenContainerLinkPath(int pid, const std::string& containerType)
{
    std::ostringstream buf;
    buf << "/proc/" << pid << "/container/" << containerType;
    return buf.str();
}

std::string ReadlinkContainer(int pid, const std::string& containerType)
{
    char buf[PATH_MAX] = {0};
    auto path = GenContainerLinkPath(pid, containerType);
    ssize_t nbytes = readlink(path.c_str(), buf, PATH_MAX);
    if (nbytes == -1) {
        printf("pid %d, ReadlinkContainer readlink %s failed, errno=%d\n", getpid(), path.c_str(), errno);
        return path.c_str();
    }
    return buf;
}

using namespace testing::ext;
namespace OHOS {
class ContainerTest : public testing::Test {
public:
    static void SetUpTestCase(void) {}
    static void TearDownTestCase(void) {}

protected:
    virtual void SetUp();
    virtual void TearDown();
};

#if defined(LOSCFG_USER_TEST_SMOKE)
HWTEST_F(ContainerTest, ItContainer001, TestSize.Level0)
{
    ItContainer001();
}

#if defined(LOSCFG_USER_TEST_PID_CONTAINER)
/**
* @tc.name: Container_Pid_Test_023
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer023, TestSize.Level0)
{
    ItPidContainer023();
}

/**
* @tc.name: Container_Pid_Test_025
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer025, TestSize.Level0)
{
    ItPidContainer025();
}

/**
* @tc.name: Container_Pid_Test_026
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer026, TestSize.Level0)
{
    ItPidContainer026();
}

/**
* @tc.name: Container_Pid_Test_027
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer027, TestSize.Level0)
{
    ItPidContainer027();
}

/**
* @tc.name: Container_Pid_Test_028
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer028, TestSize.Level0)
{
    ItPidContainer028();
}

/**
* @tc.name: Container_Pid_Test_029
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer029, TestSize.Level0)
{
    ItPidContainer029();
}

/**
* @tc.name: Container_Pid_Test_030
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer030, TestSize.Level0)
{
    ItPidContainer030();
}

/**
* @tc.name: Container_Pid_Test_031
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI6D9Y0
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer031, TestSize.Level0)
{
    ItPidContainer031();
}
#endif
#if defined(LOSCFG_USER_TEST_UTS_CONTAINER)
/**
* @tc.name: Container_UTS_Test_001
* @tc.desc: uts container function test case
* @tc.type: FUNC
* @tc.require: issueI6A7C8
* @tc.author:
*/
HWTEST_F(ContainerTest, ItUtsContainer001, TestSize.Level0)
{
    ItUtsContainer001();
}

/**
* @tc.name: Container_UTS_Test_002
* @tc.desc: uts container function test case
* @tc.type: FUNC
* @tc.require: issueI6A7C8
* @tc.author:
*/
HWTEST_F(ContainerTest, ItUtsContainer002, TestSize.Level0)
{
    ItUtsContainer002();
}

/**
* @tc.name: Container_UTS_Test_004
* @tc.desc: uts container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItUtsContainer004, TestSize.Level0)
{
    ItUtsContainer004();
}

/**
* @tc.name: Container_UTS_Test_005
* @tc.desc: uts container function test case
* @tc.type: FUNC
* @tc.require: issueI6D9Y0
* @tc.author:
*/
HWTEST_F(ContainerTest, ItUtsContainer005, TestSize.Level0)
{
    ItUtsContainer005();
}

/**
* @tc.name: Container_UTS_Test_006
* @tc.desc: uts container function test case
* @tc.type: FUNC
* @tc.require: issueI6D9Y0
* @tc.author:
*/
HWTEST_F(ContainerTest, ItUtsContainer006, TestSize.Level0)
{
    ItUtsContainer006();
}
#endif

#if defined(LOSCFG_USER_TEST_MNT_CONTAINER)
/**
* @tc.name: Container_MNT_Test_001
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6APW2
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer001, TestSize.Level0)
{
    ItMntContainer001();
}

/**
* @tc.name: Container_MNT_Test_002
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6APW2
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer002, TestSize.Level0)
{
    ItMntContainer002();
}

/**
* @tc.name: Container_MNT_Test_003
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6APW2
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer003, TestSize.Level0)
{
    ItMntContainer003();
}

/**
* @tc.name: Container_MNT_Test_004
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6APW2
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer004, TestSize.Level0)
{
    ItMntContainer004();
}

/**
* @tc.name: Container_MNT_Test_005
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer005, TestSize.Level0)
{
    ItMntContainer005();
}

/**
* @tc.name: Container_MNT_Test_006
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer006, TestSize.Level0)
{
    ItMntContainer006();
}

/**
* @tc.name: Container_MNT_Test_007
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer007, TestSize.Level0)
{
    ItMntContainer007();
}

/**
* @tc.name: Container_MNT_Test_008
* @tc.desc: mnt container function test case
* @tc.type: FUNC
* @tc.require: issueI6D9Y0
* @tc.author:
*/
HWTEST_F(ContainerTest, ItMntContainer008, TestSize.Level0)
{
    ItMntContainer008();
}

/**
* @tc.name: chroot_Test_001
* @tc.desc: chroot function test case
* @tc.type: FUNC
* @tc.require: issueI6APW2
* @tc.author:
*/
HWTEST_F(ContainerTest, ItContainerChroot001, TestSize.Level0)
{
    ItContainerChroot001();
}

/**
* @tc.name: chroot_Test_002
* @tc.desc: chroot function test case
* @tc.type: FUNC
* @tc.require: issueI6APW2
* @tc.author:
*/
HWTEST_F(ContainerTest, ItContainerChroot002, TestSize.Level0)
{
    ItContainerChroot002();
}
#endif /* LOSCFG_USER_TEST_MNT_CONTAINER */

#if defined(LOSCFG_USER_TEST_IPC_CONTAINER)
/**
* @tc.name: Container_IPC_Test_001
* @tc.desc: ipc container function test case
* @tc.type: FUNC
* @tc.require: issueI6AVMY
* @tc.author:
*/
HWTEST_F(ContainerTest, ItIpcContainer001, TestSize.Level0)
{
    ItIpcContainer001();
}

/**
* @tc.name: Container_IPC_Test_002
* @tc.desc: ipc container function test case
* @tc.type: FUNC
* @tc.require: issueI6D9Y0
* @tc.author:
*/
HWTEST_F(ContainerTest, ItIpcContainer002, TestSize.Level0)
{
    ItIpcContainer002();
}

/**
* @tc.name: Container_IPC_Test_003
* @tc.desc: ipc container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItIpcContainer003, TestSize.Level0)
{
    ItIpcContainer003();
}

/**
* @tc.name: Container_IPC_Test_004
* @tc.desc: ipc container function test case
* @tc.type: FUNC
* @tc.require: issueI6AVMY
* @tc.author:
*/
HWTEST_F(ContainerTest, ItIpcContainer004, TestSize.Level0)
{
    ItIpcContainer004();
}

/**
* @tc.name: Container_IPC_Test_005
* @tc.desc: ipc container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItIpcContainer005, TestSize.Level0)
{
    ItIpcContainer005();
}

/**
* @tc.name: Container_IPC_Test_006
* @tc.desc: ipc container function test case
* @tc.type: FUNC
* @tc.require: issueI6D9Y0
* @tc.author:
*/
HWTEST_F(ContainerTest, ItIpcContainer006, TestSize.Level0)
{
    ItIpcContainer006();
}
#endif

#if defined(LOSCFG_USER_TEST_TIME_CONTAINER)
/**
* @tc.name: Container_TIME_Test_001
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6B0A3
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer001, TestSize.Level0)
{
    ItTimeContainer001();
}

/**
* @tc.name: Container_TIME_Test_002
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer002, TestSize.Level0)
{
    ItTimeContainer002();
}

/**
* @tc.name: Container_TIME_Test_003
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6D9Y0
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer003, TestSize.Level0)
{
    ItTimeContainer003();
}

/**
* @tc.name: Container_TIME_Test_004
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer004, TestSize.Level0)
{
    ItTimeContainer004();
}

/**
* @tc.name: Container_TIME_Test_005
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer005, TestSize.Level0)
{
    ItTimeContainer005();
}

/*
* @tc.name: Container_TIME_Test_007
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6B0A3
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer007, TestSize.Level0)
{
    ItTimeContainer007();
}

/**
* @tc.name: Container_TIME_Test_008
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6BE5A
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer008, TestSize.Level0)
{
    ItTimeContainer008();
}

/**
* @tc.name: Container_TIME_Test_009
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6B0A3
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer009, TestSize.Level0)
{
    ItTimeContainer009();
}

/**
* @tc.name: Container_TIME_Test_010
* @tc.desc: time container function test case
* @tc.type: FUNC
* @tc.require: issueI6B0A3
* @tc.author:
*/
HWTEST_F(ContainerTest, ItTimeContainer010, TestSize.Level0)
{
    ItTimeContainer010();
}
#endif
#endif /* LOSCFG_USER_TEST_SMOKE */

#if defined(LOSCFG_USER_TEST_FULL)
#if defined(LOSCFG_USER_TEST_PID_CONTAINER)
/**
* @tc.name: Container_Pid_Test_001
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer001, TestSize.Level0)
{
    ItPidContainer001();
}

/**
* @tc.name: Container_Pid_Test_002
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer002, TestSize.Level0)
{
    ItPidContainer002();
}

/**
* @tc.name: Container_Pid_Test_003
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer003, TestSize.Level0)
{
    ItPidContainer003();
}

/**
* @tc.name: Container_Pid_Test_004
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer004, TestSize.Level0)
{
    ItPidContainer004();
}

#if defined(LOSCFG_USER_TEST_UTS_CONTAINER)
/**
* @tc.name: Container_Pid_Test_005
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer005, TestSize.Level0)
{
    ItPidContainer005();
}
#endif

/**
* @tc.name: Container_Pid_Test_006
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer006, TestSize.Level0)
{
    ItPidContainer006();
}

/**
* @tc.name: Container_Pid_Test_007
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer007, TestSize.Level0)
{
    ItPidContainer007();
}

/**
* @tc.name: Container_Pid_Test_008
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer008, TestSize.Level0)
{
    ItPidContainer008();
}

/**
* @tc.name: Container_Pid_Test_009
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer009, TestSize.Level0)
{
    ItPidContainer009();
}

/**
* @tc.name: Container_Pid_Test_010
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer010, TestSize.Level0)
{
    ItPidContainer010();
}

/**
* @tc.name: Container_Pid_Test_011
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer011, TestSize.Level0)
{
    ItPidContainer011();
}

/**
* @tc.name: Container_Pid_Test_012
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer012, TestSize.Level0)
{
    ItPidContainer012();
}

/**
* @tc.name: Container_Pid_Test_013
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer013, TestSize.Level0)
{
    ItPidContainer013();
}

/**
* @tc.name: Container_Pid_Test_014
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer014, TestSize.Level0)
{
    ItPidContainer014();
}

/**
* @tc.name: Container_Pid_Test_015
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer015, TestSize.Level0)
{
    ItPidContainer015();
}

/**
* @tc.name: Container_Pid_Test_016
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer016, TestSize.Level0)
{
    ItPidContainer016();
}

/**
* @tc.name: Container_Pid_Test_017
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer017, TestSize.Level0)
{
    ItPidContainer017();
}

/**
* @tc.name: Container_Pid_Test_018
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer018, TestSize.Level0)
{
    ItPidContainer018();
}

/**
* @tc.name: Container_Pid_Test_019
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer019, TestSize.Level0)
{
    ItPidContainer019();
}

/**
* @tc.name: Container_Pid_Test_020
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer020, TestSize.Level0)
{
    ItPidContainer020();
}

/**
* @tc.name: Container_Pid_Test_021
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer021, TestSize.Level0)
{
    ItPidContainer021();
}

/**
* @tc.name: Container_Pid_Test_022
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer022, TestSize.Level0)
{
    ItPidContainer022();
}

/**
* @tc.name: Container_Pid_Test_024
* @tc.desc: pid container function test case
* @tc.type: FUNC
* @tc.require: issueI68LVW
* @tc.author:
*/
HWTEST_F(ContainerTest, ItPidContainer024, TestSize.Level0)
{
    ItPidContainer024();
}
#endif
#if defined(LOSCFG_USER_TEST_UTS_CONTAINER)
/**
* @tc.name: Container_UTS_Test_003
* @tc.desc: uts container function test case
* @tc.type: FUNC
* @tc.require: issueI6A7C8
* @tc.author:
*/
HWTEST_F(ContainerTest, ItUtsContainer003, TestSize.Level0)
{
    ItUtsContainer003();
}
#endif
#endif
} // namespace OHOS

namespace OHOS {
void ContainerTest::SetUp()
{
    mode_t mode = 0;
    (void)mkdir(ACCESS_FILE_NAME, S_IFDIR | mode);
}
void ContainerTest::TearDown()
{
    (void)rmdir(ACCESS_FILE_NAME);
}
}
