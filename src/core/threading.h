#ifndef DCAT_THREADING_H
#define DCAT_THREADING_H

#include <stdbool.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>

typedef CRITICAL_SECTION DcatMutex;
typedef HANDLE DcatThread;
typedef unsigned(__stdcall *DcatThreadFunc)(void *);

static bool dcat_mutex_init(DcatMutex *mutex) {
  InitializeCriticalSection(mutex);
  return true;
}

static void dcat_mutex_lock(DcatMutex *mutex) {
  EnterCriticalSection(mutex);
}

static void dcat_mutex_unlock(DcatMutex *mutex) {
  LeaveCriticalSection(mutex);
}

static void dcat_mutex_destroy(DcatMutex *mutex) {
  DeleteCriticalSection(mutex);
}

static bool dcat_thread_create(DcatThread *thread, DcatThreadFunc func,
                                      void *arg) {
  const uintptr_t handle = _beginthreadex(NULL, 0, func, arg, 0, NULL);
  if (handle == 0) {
    return false;
  }
  *thread = (HANDLE)handle;
  return true;
}

static bool dcat_thread_join(DcatThread thread) {
  if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
    return false;
  }
  CloseHandle(thread);
  return true;
}

static void dcat_sleep_ms(const unsigned int ms) {
  Sleep(ms);
}

#else

#include <pthread.h>
#include <time.h>

typedef pthread_mutex_t DcatMutex;
typedef pthread_t DcatThread;
typedef void *(*DcatThreadFunc)(void *);

static inline bool dcat_mutex_init(DcatMutex *mutex) {
  return pthread_mutex_init(mutex, NULL) == 0;
}

static inline void dcat_mutex_lock(DcatMutex *mutex) {
  pthread_mutex_lock(mutex);
}

static inline void dcat_mutex_unlock(DcatMutex *mutex) {
  pthread_mutex_unlock(mutex);
}

static inline void dcat_mutex_destroy(DcatMutex *mutex) {
  pthread_mutex_destroy(mutex);
}

static inline bool dcat_thread_create(DcatThread *thread, DcatThreadFunc func,
                                      void *arg) {
  return pthread_create(thread, NULL, func, arg) == 0;
}

static inline bool dcat_thread_join(DcatThread thread) {
  return pthread_join(thread, NULL) == 0;
}

static inline void dcat_sleep_ms(unsigned int ms) {
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000);
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

#endif

#endif // DCAT_THREADING_H
