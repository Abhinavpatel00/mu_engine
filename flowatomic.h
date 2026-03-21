#ifndef MU_ATOMICS_H
#define MU_ATOMICS_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
   Compiler detection
   ============================================================ */

#if defined(_MSC_VER)
    #define MU_COMPILER_MSVC 1
    #include <intrin.h>
    #include <windows.h>
#else
    #define MU_COMPILER_GCC 1
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

typedef volatile ALIGNAS(4)        uint32_t  mu_atomic32_t;
typedef volatile ALIGNAS(8)        uint64_t  mu_atomic64_t;
typedef volatile ALIGNAS(PTR_SIZE) uintptr_t mu_atomicptr_t;

/* ============================================================
   Compiler memory barriers (NOT CPU fences)
   ============================================================ */

#if MU_COMPILER_MSVC
    #define mu_memorybarrier_acquire() _ReadWriteBarrier()
    #define mu_memorybarrier_release() _ReadWriteBarrier()
#else
    #define mu_memorybarrier_acquire() __asm__ __volatile__("" ::: "memory")
    #define mu_memorybarrier_release() __asm__ __volatile__("" ::: "memory")
#endif

/* ============================================================
   32-bit atomics (relaxed)
   ============================================================ */

#if MU_COMPILER_MSVC

#define mu_atomic32_load_relaxed(p)        (*(p))
#define mu_atomic32_store_relaxed(p, v)    (uint32_t)InterlockedExchange((volatile long*)(p), (long)(v))
#define mu_atomic32_add_relaxed(p, v)      (uint32_t)InterlockedExchangeAdd((volatile long*)(p), (long)(v))
#define mu_atomic32_cas_relaxed(p,c,n)     (uint32_t)InterlockedCompareExchange((volatile long*)(p),(long)(n),(long)(c))

#else

#define mu_atomic32_load_relaxed(p)        (*(p))
#define mu_atomic32_store_relaxed(p, v)    __sync_lock_test_and_set((p),(v))
#define mu_atomic32_add_relaxed(p, v)      __sync_fetch_and_add((p),(v))
#define mu_atomic32_cas_relaxed(p,c,n)     __sync_val_compare_and_swap((p),(c),(n))

#endif

/* ============================================================
   64-bit atomics (relaxed)
   ============================================================ */

#if MU_COMPILER_MSVC

#define mu_atomic64_load_relaxed(p)        (*(p))
#define mu_atomic64_store_relaxed(p, v)    (uint64_t)InterlockedExchange64((volatile LONG64*)(p),(LONG64)(v))
#define mu_atomic64_add_relaxed(p, v)      (uint64_t)InterlockedExchangeAdd64((volatile LONG64*)(p),(LONG64)(v))
#define mu_atomic64_cas_relaxed(p,c,n)     (uint64_t)InterlockedCompareExchange64((volatile LONG64*)(p),(LONG64)(n),(LONG64)(c))

#else

#define mu_atomic64_load_relaxed(p)        (*(p))
#define mu_atomic64_store_relaxed(p, v)    __sync_lock_test_and_set((p),(v))
#define mu_atomic64_add_relaxed(p, v)      __sync_fetch_and_add((p),(v))
#define mu_atomic64_cas_relaxed(p,c,n)     __sync_val_compare_and_swap((p),(c),(n))

#endif

/* ============================================================
   Acquire / Release helpers
   ============================================================ */

static inline uint32_t mu_atomic32_load_acquire(mu_atomic32_t* p)
{
    uint32_t v = mu_atomic32_load_relaxed(p);
    mu_memorybarrier_acquire();
    return v;
}

static inline uint32_t mu_atomic32_store_release(mu_atomic32_t* p, uint32_t v)
{
    mu_memorybarrier_release();
    return mu_atomic32_store_relaxed(p, v);
}

static inline uint64_t mu_atomic64_load_acquire(mu_atomic64_t* p)
{
    uint64_t v = mu_atomic64_load_relaxed(p);
    mu_memorybarrier_acquire();
    return v;
}

static inline uint64_t mu_atomic64_store_release(mu_atomic64_t* p, uint64_t v)
{
    mu_memorybarrier_release();
    return mu_atomic64_store_relaxed(p, v);
}

/* ============================================================
   Atomic max (relaxed)
   - Updates *dst to max(*dst, val)
   - Returns previous value
   ============================================================ */

static inline uint32_t mu_atomic32_max_relaxed(mu_atomic32_t* dst, uint32_t val)
{
    uint32_t cur = val;
    do {
        cur = mu_atomic32_cas_relaxed(dst, cur, val);
    } while (cur < val);
    return cur;
}

static inline uint64_t mu_atomic64_max_relaxed(mu_atomic64_t* dst, uint64_t val)
{
    uint64_t cur = val;
    do {
        cur = mu_atomic64_cas_relaxed(dst, cur, val);
    } while (cur < val);
    return cur;
}

/* ============================================================
   Pointer-sized atomics
   ============================================================ */

#if PTR_SIZE == 4

#define mu_atomicptr_load_relaxed   mu_atomic32_load_relaxed
#define mu_atomicptr_load_acquire   mu_atomic32_load_acquire
#define mu_atomicptr_store_relaxed  mu_atomic32_store_relaxed
#define mu_atomicptr_store_release  mu_atomic32_store_release
#define mu_atomicptr_add_relaxed    mu_atomic32_add_relaxed
#define mu_atomicptr_cas_relaxed    mu_atomic32_cas_relaxed
#define mu_atomicptr_max_relaxed    mu_atomic32_max_relaxed

#elif PTR_SIZE == 8

#define mu_atomicptr_load_relaxed   mu_atomic64_load_relaxed
#define mu_atomicptr_load_acquire   mu_atomic64_load_acquire
#define mu_atomicptr_store_relaxed  mu_atomic64_store_relaxed
#define mu_atomicptr_store_release  mu_atomic64_store_release
#define mu_atomicptr_add_relaxed    mu_atomic64_add_relaxed
#define mu_atomicptr_cas_relaxed    mu_atomic64_cas_relaxed
#define mu_atomicptr_max_relaxed    mu_atomic64_max_relaxed

#endif

#endif /* MU_ATOMICS_H */
