#ifndef SOCKET_HELPER_H
#define SOCKET_HELPER_H

#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define CLOSE_SOCKET closesocket
    #define IS_VALIDSOCKET(s) ((s) != INVALID_SOCKET)
    inline bool init_network() {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }
    inline void cleanup_network() {
        WSACleanup();
    }
    #define THREAD_ROUTINE DWORD WINAPI
    typedef LPVOID THREAD_ARG;
    typedef HANDLE THREAD_TYPE;
    inline THREAD_TYPE start_thread(THREAD_ROUTINE (*func)(THREAD_ARG), THREAD_ARG arg) {
        HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
        if (h != NULL) {
            CloseHandle(h); // Detach thread by closing its handle on Windows
        }
        return h;
    }
    struct Mutex {
        CRITICAL_SECTION cs;
        Mutex() { InitializeCriticalSection(&cs); }
        ~Mutex() { DeleteCriticalSection(&cs); }
        void lock() { EnterCriticalSection(&cs); }
        void unlock() { LeaveCriticalSection(&cs); }
    };
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <pthread.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define CLOSE_SOCKET ::close
    #define IS_VALIDSOCKET(s) ((s) >= 0)
    inline bool init_network() { return true; }
    inline void cleanup_network() {}
    #define THREAD_ROUTINE void*
    typedef void* THREAD_ARG;
    typedef pthread_t THREAD_TYPE;
    inline THREAD_TYPE start_thread(THREAD_ROUTINE (*func)(THREAD_ARG), THREAD_ARG arg) {
        pthread_t t;
        pthread_create(&t, NULL, func, arg);
        pthread_detach(t);
        return t;
    }
    struct Mutex {
        pthread_mutex_t m;
        Mutex() { pthread_mutex_init(&m, NULL); }
        ~Mutex() { pthread_mutex_destroy(&m); }
        void lock() { pthread_mutex_lock(&m); }
        void unlock() { pthread_mutex_unlock(&m); }
    };
#endif

struct LockGuard {
    Mutex& m;
    LockGuard(Mutex& mutex) : m(mutex) { m.lock(); }
    ~LockGuard() { m.unlock(); }
};

#endif // SOCKET_HELPER_H
