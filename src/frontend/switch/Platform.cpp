#include <stdio.h>
#include <string.h>

#include "../../Platform.h"

#include <switch.h>

namespace Platform
{

void StopEmu()
{
}

FILE* OpenFile(const char* path, const char* mode, bool mustexist)
{
    FILE* ret;

    if (mustexist)
    {
        ret = fopen(path, "rb");
        if (ret) ret = freopen(path, mode, ret);
    }
    else
        ret = fopen(path, mode);

    return ret;
}


FILE* OpenLocalFile(const char* path, const char* mode)
{
    char finalPath[256];
    sprintf(finalPath, "/switch/melonds/%s", path);

    FILE* ret = fopen(finalPath, mode);
    if (ret)
        return ret;

    sprintf(finalPath, "/melonds/%s", path);

    ret = fopen(finalPath, mode);

    return ret;
}

FILE* OpenDataFile(const char* path)
{
	return OpenLocalFile(path, "rb");
}

void ThreadEntry(void* param)
{
    ((void (*)())param)();
}

#define STACK_SIZE (1024 * 1024 * 4)

Thread* Thread_Create(void (*func)())
{
    ::Thread* thread = new ::Thread();
    auto res = threadCreate(thread, ThreadEntry, (void*)func, NULL, STACK_SIZE, 0x2C, 2);
    threadStart(thread);
    return (Thread*)thread;
}

void Thread_Free(Thread* thread)
{
    threadClose((::Thread*)thread);
    delete ((::Thread*)thread);
}

void Thread_Wait(Thread* thread)
{
    threadWaitForExit((::Thread*)thread);
}

Semaphore* Semaphore_Create()
{
    ::Semaphore* sema = new ::Semaphore();
    semaphoreInit(sema, 0);
    return (Semaphore*)sema;
}

void Semaphore_Free(Semaphore* sema)
{
    delete (::Semaphore*)sema;
}

void Semaphore_Reset(Semaphore* sema)
{
    while(semaphoreTryWait((::Semaphore*)sema));
}

void Semaphore_Wait(Semaphore* sema)
{
    semaphoreWait((::Semaphore*)sema);
}

void Semaphore_Post(Semaphore* sema, int num)
{
    for (int i = 0; i < num; i++)
        semaphoreSignal((::Semaphore*)sema);
}

Mutex* Mutex_Create()
{
    ::Mutex* mutex = new ::Mutex();
    mutexInit(mutex);
    return (Mutex*)mutex;
}

void Mutex_Free(Mutex* mutex)
{
    delete (::Mutex*)mutex;
}

void Mutex_Lock(Mutex* mutex)
{
    mutexLock((::Mutex*)mutex);
}

void Mutex_Unlock(Mutex* mutex)
{
    mutexUnlock((::Mutex*)mutex);
}

bool Mutex_TryLock(Mutex* mutex)
{
    return mutexTryLock((::Mutex*)mutex);
}

void* GL_GetProcAddress(const char* proc)
{
    return NULL;
}

bool MP_Init()
{
    return false;
}

void MP_DeInit()
{}

int MP_SendPacket(u8* data, int len)
{
    return 0;
}

int MP_RecvPacket(u8* data, bool block)
{
    return 0;
}

bool LAN_Init()
{
    return false;
}

void LAN_DeInit()
{}

int LAN_SendPacket(u8* data, int len)
{
    return 0;
}

int LAN_RecvPacket(u8* data)
{
    return 0;
}

}