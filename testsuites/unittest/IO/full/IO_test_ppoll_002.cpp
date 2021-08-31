#include "It_test_IO.h"
#include "signal.h"
#include "pthread.h"

#define BUF_SIZE 128
#define DELAY_TIME 200

static int pipeFdPpoll[2];
static int g_step = 1;
static char strBuf[] = "hello world.";
static struct pollfd pfd;

static void *pthread_01(void *arg)
{
    int retVal;
    char buf[BUF_SIZE];
    const struct timespec timeout = {10000, 0};
    
    /* 执行ppoll监视文件描述符 */
    while(g_step < 4) {
        usleep(DELAY_TIME);
    }
    g_step++;
    retVal = ppoll(&pfd, 1, &timeout, NULL);
    ICUNIT_ASSERT_NOT_EQUAL_NULL(retVal, -1, retVal);
    
    /* 判断revents */
    if (pfd.revents & POLLIN) {
        memset(buf, 0, sizeof(buf));
        retVal = read(pfd.fd, buf, BUF_SIZE);
        ICUNIT_ASSERT_NOT_EQUAL_NULL(retVal, -1, retVal);
        retVal = strcmp(strBuf, buf);
        ICUNIT_ASSERT_EQUAL_NULL(retVal, 0, retVal);
    }
    
    while(g_step < 5) {
        usleep(DELAY_TIME);
    }
    pthread_exit(NULL);
}

static UINT32 testcase(VOID)
{
    int retVal;
    pthread_t tid;
    
    /* 建立管道 */
    while(g_step < 1) {
        usleep(DELAY_TIME);
    }
    retVal = pipe(pipeFdPpoll);
    ICUNIT_ASSERT_NOT_EQUAL(retVal, -1, retVal);
    g_step++;
    
    /* 设置pfd */
    pfd.fd = pipeFdPpoll[0];
    pfd.events = POLLIN;
    
    /* 向管道写入数据 */
    while(g_step < 2) {
        usleep(DELAY_TIME);
    }
    sleep(1);
    
    retVal = write(pipeFdPpoll[1], "hello world.", sizeof(strBuf));
    ICUNIT_ASSERT_NOT_EQUAL(retVal, -1, retVal);
    g_step++;

    /* 开辟线程执行 ppoll */
    while(g_step < 3) {
        usleep(DELAY_TIME);
    }
    retVal = pthread_create(&tid, NULL, pthread_01, NULL);
    ICUNIT_ASSERT_EQUAL(retVal, 0, retVal);
    g_step++;

    pthread_join(tid, NULL);
    
    return LOS_OK;    
}

VOID IO_TEST_PPOLL_002(VOID)
{
    TEST_ADD_CASE(__FUNCTION__, testcase, TEST_LIB, TEST_LIBC, TEST_LEVEL1, TEST_FUNCTION);
}