#include "It_test_IO.h"
#include "pthread.h"
#include "signal.h"

#define BUF_SIZE 128
#define DELAY_TIME 200

STATIC INT32 pipeFdPpoll[2];
STATIC INT32 g_step = 1;
STATIC CHAR strBuf[] = "hello world.";
STATIC struct pollfd pfd;
sigset_t sigMask;
STATIC UINT32 count = 0;

STATIC VOID signalHandle(INT32 sigNum)
{
    //printf("Capture %d\n", sigNum);
    g_step++;

    return;
}

STATIC VOID *pthread_01(VOID *arg)
{
    INT32 retVal;
    CHAR buf[BUF_SIZE];

    signal(SIGUSR1, signalHandle);
    
    while (1) {
        /*执行ppoll监视文件描述符*/
        while (g_step < 2) {
            usleep(DELAY_TIME);
        }
        g_step++;
        retVal = ppoll(&pfd, 1, NULL, &sigMask);

        ICUNIT_ASSERT_NOT_EQUAL_NULL(retVal, -1, retVal);
        
        /*判断revents*/
        if (pfd.revents & POLLIN) {
            memset(buf, 0, sizeof(buf));
            retVal = read(pfd.fd, buf, BUF_SIZE);
            ICUNIT_ASSERT_NOT_EQUAL_NULL(retVal, -1, retVal);

            retVal = strcmp(strBuf, buf);
            ICUNIT_ASSERT_EQUAL_NULL(retVal, 0, retVal);

            count++;
        } else {
            ICUNIT_ASSERT_NOT_EQUAL_NULL(pfd.revents & POLLIN, 0, pfd.revents & POLLIN);
        }
        g_step++;   
        
        if (g_step >= 7) {
            ICUNIT_ASSERT_EQUAL_NULL(count, 2, count);
            pthread_exit(NULL);
        }
        
    }

    return LOS_OK;
}

STATIC UINT32 testcase(VOID)
{
    INT32 retVal;
    pthread_t tid;
    
    /*建立管道*/
    while (g_step < 1) {
        usleep(DELAY_TIME);
    }
    retVal = pipe(pipeFdPpoll);
    ICUNIT_ASSERT_NOT_EQUAL(retVal, -1, retVal);
    
    /*设置pfd sigmask*/
    pfd.fd = pipeFdPpoll[0];                    
    pfd.events = POLLIN;
    pfd.revents = 0x0;

    sigemptyset(&sigMask);
    sigaddset(&sigMask, SIGUSR1);
    
    /*开辟线程执行 ppoll*/
    retVal = pthread_create(&tid, NULL, pthread_01, NULL);
    ICUNIT_ASSERT_EQUAL(retVal, 0, retVal);
    g_step++;
    
    /*向管道写入数据*/
    while (g_step < 3) {
        usleep(DELAY_TIME);
    }
    sleep(1);                                     /*保证先挂起再写入数据*/
    retVal = write(pipeFdPpoll[1], "hello world.", sizeof(strBuf));
    ICUNIT_ASSERT_NOT_EQUAL(retVal, -1, retVal);

    /*向线程发送信号*/
    while (g_step < 5) {
        usleep(DELAY_TIME);
    }
    sleep(1);                                    /*保证先挂起再发送信号*/
    retVal = pthread_kill(tid, SIGUSR1);
    ICUNIT_ASSERT_EQUAL(retVal, 0, retVal);

    /*继续向管道写入数据*/ 
    ICUNIT_ASSERT_EQUAL(g_step, 5, g_step);     /*判断挂起解除之前信号没有被处理*/
    retVal = write(pipeFdPpoll[1], "hello world.", sizeof(strBuf));
    ICUNIT_ASSERT_NOT_EQUAL(retVal, -1, retVal);

    while (g_step < 7) {
        usleep(DELAY_TIME);
    }
    ICUNIT_ASSERT_EQUAL(count, 2, count);
    /*等待退出*/
    pthread_join(tid, NULL);
    
    return LOS_OK;    
}

VOID IO_TEST_PPOLL_003(VOID)
{
    TEST_ADD_CASE(__FUNCTION__, testcase, TEST_LIB, TEST_LIBC, TEST_LEVEL1, TEST_FUNCTION);
}