#ifndef FLOW_ATOMICS_H
#define FLOW_ATOMICS_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
   Compiler detection
   ============================================================ */

#if defined(_MSC_VER)
    #define FLOW_COMPILER_MSVC 1
    #include <intrin.h>
    #include <windows.h>
#else
    #define FLOW_COMPILER_GCC 1
#endif

/* ============================================================
   Alignment helper
   ============================================================ */

#if defined(_MSC_VER)
    #define ALIGNAS(x) __declspec(align(x))
#elif defined(__GNUC__)
    #define ALIGNAS(x) __attribute__((aligned(x)))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define ALIGNAS(x) _Alignas(x)
#else
    #define ALIGNAS(x)
#endif

/* ============================================================
   Pointer size
   ============================================================ */

#define PTR_SIZE 8

/* ============================================================
   Atomic types
   NOTE:
   - volatile + compiler barriers
   - relies on x86 TSO for correctness
   ============================================================ */

typedef volatile ALIGNAS(4)        uint32_t  flow_atomic32_t;
typedef volatile ALIGNAS(8)        uint64_t  flow_atomic64_t;
typedef volatile ALIGNAS(PTR_SIZE) uintptr_t flow_atomicptr_t;

/* ============================================================
   Compiler memory barriers (NOT CPU fences)
   ============================================================ */

#if FLOW_COMPILER_MSVC
    #define flow_memorybarrier_acquire() _ReadWriteBarrier()
    #define flow_memorybarrier_release() _ReadWriteBarrier()
#else
    #define flow_memorybarrier_acquire() __asm__ __volatile__("" ::: "memory")
    #define flow_memorybarrier_release() __asm__ __volatile__("" ::: "memory")
#endif

/* ============================================================
   32-bit atomics (relaxed)
   ============================================================ */

#if FLOW_COMPILER_MSVC

#define flow_atomic32_load_relaxed(p)        (*(p))
#define flow_atomic32_store_relaxed(p, v)    (uint32_t)InterlockedExchange((volatile long*)(p), (long)(v))
#define flow_atomic32_add_relaxed(p, v)      (uint32_t)InterlockedExchangeAdd((volatile long*)(p), (long)(v))
#define flow_atomic32_cas_relaxed(p,c,n)     (uint32_t)InterlockedCompareExchange((volatile long*)(p),(long)(n),(long)(c))

#else

#define flow_atomic32_load_relaxed(p)        (*(p))
#define flow_atomic32_store_relaxed(p, v)    __sync_lock_test_and_set((p),(v))
#define flow_atomic32_add_relaxed(p, v)      __sync_fetch_and_add((p),(v))
#define flow_atomic32_cas_relaxed(p,c,n)     __sync_val_compare_and_swap((p),(c),(n))

#endif

/* ============================================================
   64-bit atomics (relaxed)
   ============================================================ */

#if FLOW_COMPILER_MSVC

#define flow_atomic64_load_relaxed(p)        (*(p))
#define flow_atomic64_store_relaxed(p, v)    (uint64_t)InterlockedExchange64((volatile LONG64*)(p),(LONG64)(v))
#define flow_atomic64_add_relaxed(p, v)      (uint64_t)InterlockedExchangeAdd64((volatile LONG64*)(p),(LONG64)(v))
#define flow_atomic64_cas_relaxed(p,c,n)     (uint64_t)InterlockedCompareExchange64((volatile LONG64*)(p),(LONG64)(n),(LONG64)(c))

#else

#define flow_atomic64_load_relaxed(p)        (*(p))
#define flow_atomic64_store_relaxed(p, v)    __sync_lock_test_and_set((p),(v))
#define flow_atomic64_add_relaxed(p, v)      __sync_fetch_and_add((p),(v))
#define flow_atomic64_cas_relaxed(p,c,n)     __sync_val_compare_and_swap((p),(c),(n))

#endif

/* ============================================================
   Acquire / Release helpers
   ============================================================ */

static inline uint32_t flow_atomic32_load_acquire(flow_atomic32_t* p)
{
    uint32_t v = flow_atomic32_load_relaxed(p);
    flow_memorybarrier_acquire();
    return v;
}

static inline uint32_t flow_atomic32_store_release(flow_atomic32_t* p, uint32_t v)
{
    flow_memorybarrier_release();
    return flow_atomic32_store_relaxed(p, v);
}

static inline uint64_t flow_atomic64_load_acquire(flow_atomic64_t* p)
{
    uint64_t v = flow_atomic64_load_relaxed(p);
    flow_memorybarrier_acquire();
    return v;
}

static inline uint64_t flow_atomic64_store_release(flow_atomic64_t* p, uint64_t v)
{
    flow_memorybarrier_release();
    return flow_atomic64_store_relaxed(p, v);
}

/* ============================================================
   Atomic max (relaxed)
   - Updates *dst to max(*dst, val)
   - Returns previous value
   ============================================================ */

static inline uint32_t flow_atomic32_max_relaxed(flow_atomic32_t* dst, uint32_t val)
{
    uint32_t cur = val;
    do {
        cur = flow_atomic32_cas_relaxed(dst, cur, val);
    } while (cur < val);
    return cur;
}

static inline uint64_t flow_atomic64_max_relaxed(flow_atomic64_t* dst, uint64_t val)
{
    uint64_t cur = val;
    do {
        cur = flow_atomic64_cas_relaxed(dst, cur, val);
    } while (cur < val);
    return cur;
}

/* ============================================================
   Pointer-sized atomics
   ============================================================ */

#if PTR_SIZE == 4

#define flow_atomicptr_load_relaxed   flow_atomic32_load_relaxed
#define flow_atomicptr_load_acquire   flow_atomic32_load_acquire
#define flow_atomicptr_store_relaxed  flow_atomic32_store_relaxed
#define flow_atomicptr_store_release  flow_atomic32_store_release
#define flow_atomicptr_add_relaxed    flow_atomic32_add_relaxed
#define flow_atomicptr_cas_relaxed    flow_atomic32_cas_relaxed
#define flow_atomicptr_max_relaxed    flow_atomic32_max_relaxed

#elif PTR_SIZE == 8

#define flow_atomicptr_load_relaxed   flow_atomic64_load_relaxed
#define flow_atomicptr_load_acquire   flow_atomic64_load_acquire
#define flow_atomicptr_store_relaxed  flow_atomic64_store_relaxed
#define flow_atomicptr_store_release  flow_atomic64_store_release
#define flow_atomicptr_add_relaxed    flow_atomic64_add_relaxed
#define flow_atomicptr_cas_relaxed    flow_atomic64_cas_relaxed
#define flow_atomicptr_max_relaxed    flow_atomic64_max_relaxed

#endif

#endif /* FLOW_ATOMICS_H */
