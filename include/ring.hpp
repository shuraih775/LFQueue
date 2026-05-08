#ifndef LOCKFREE_RING_HPP
#define LOCKFREE_RING_HPP

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
#include <cstring>
#include <algorithm>

namespace lockfree
{

    constexpr std::size_t CACHELINE_SIZE = 64;

    inline bool is_power_of_two(std::size_t x)
    {
        return x && ((x & (x - 1)) == 0);
    }

#if defined(__x86_64__) || defined(__i386__)
    inline void cpu_relax() noexcept
    {
        __builtin_ia32_pause();
    }
#else
    inline void cpu_relax() noexcept {}
#endif

#if defined(__GNUC__) || defined(__clang__)
#define LF_ALWAYS_INLINE __attribute__((always_inline)) inline
#define LF_LIKELY(x) __builtin_expect(!!(x), 1)
#define LF_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LF_ALWAYS_INLINE inline
#define LF_LIKELY(x) (x)
#define LF_UNLIKELY(x) (x)
#endif

    template <typename T, bool SingleProducer = true, bool SingleConsumer = true>
    class Ring
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Ring<T> requires pointers or trivial types.");

    public:
        explicit Ring(std::size_t size, uint32_t publish_batch = 32)
            : size_(static_cast<uint32_t>(size)),
              mask_(static_cast<uint32_t>(size - 1)),
              publish_batch_(publish_batch)
        {
            if (!is_power_of_two(size_))
                throw std::invalid_argument("size must be power of two");

            buffer_ = static_cast<T *>(
                std::aligned_alloc(CACHELINE_SIZE, sizeof(T) * size_));

            if (!buffer_)
                throw std::bad_alloc();
        }

        ~Ring()
        {
            flush();
            std::free(buffer_);
        }

        LF_ALWAYS_INLINE bool enqueue(const T &item) noexcept
        {
            return enqueue_bulk(&item, 1) == 1;
        }

        LF_ALWAYS_INLINE bool dequeue(T &out) noexcept
        {
            return dequeue_bulk(&out, 1) == 1;
        }

        LF_ALWAYS_INLINE void flush() noexcept
        {
            if constexpr (SingleProducer)
            {
                if (prod_.pending_publish_ > 0)
                {
                    uint32_t head =
                        prod_.head.load(std::memory_order_relaxed);

                    prod_.tail.store(head, std::memory_order_release);
                    prod_.pending_publish_ = 0;
                }
            }
        }

        LF_ALWAYS_INLINE std::size_t enqueue_bulk(
            const T *items,
            std::size_t n) noexcept
        {
            if (LF_UNLIKELY(n == 0))
                return 0;

            uint32_t prod_head;

            if constexpr (SingleProducer)
            {
                prod_head = prod_.head.load(std::memory_order_relaxed);

                uint32_t free_entries =
                    size_ - (prod_head - prod_.cached_cons_tail_);

                if (LF_UNLIKELY(free_entries < n))
                {
                    prod_.cached_cons_tail_ =
                        cons_.tail.load(std::memory_order_acquire);

                    free_entries =
                        size_ - (prod_head - prod_.cached_cons_tail_);

                    if (LF_UNLIKELY(free_entries < n))
                        return 0;
                }

                prod_.head.store(
                    prod_head + static_cast<uint32_t>(n),
                    std::memory_order_relaxed);
            }
            else
            {
                if (LF_UNLIKELY(!mp_reserve(n, prod_head)))
                    return 0;
            }

            uint32_t idx = prod_head & mask_;

            uint32_t first =
                std::min<uint32_t>(
                    size_ - idx,
                    static_cast<uint32_t>(n));

            std::memcpy(
                &buffer_[idx],
                items,
                first * sizeof(T));

            if (first < n)
            {
                std::memcpy(
                    buffer_,
                    items + first,
                    (n - first) * sizeof(T));
            }

            if constexpr (!SingleProducer)
            {
                while (prod_.tail.load(std::memory_order_relaxed) != prod_head)
                    cpu_relax();
            }

            prod_.pending_publish_ += static_cast<uint32_t>(n);

            if (prod_.pending_publish_ >= publish_batch_)
            {
                prod_.tail.store(
                    prod_head + static_cast<uint32_t>(n),
                    std::memory_order_release);

                prod_.pending_publish_ = 0;
            }

            return n;
        }

        LF_ALWAYS_INLINE std::size_t dequeue_bulk(
            T *out,
            std::size_t n) noexcept
        {
            if (LF_UNLIKELY(n == 0))
                return 0;

            uint32_t cons_head;

            if constexpr (SingleConsumer)
            {
                cons_head = cons_.head.load(std::memory_order_relaxed);

                uint32_t entries =
                    cons_.cached_prod_tail_ - cons_head;

                if (LF_UNLIKELY(entries < n))
                {
                    cons_.cached_prod_tail_ =
                        prod_.tail.load(std::memory_order_acquire);

                    entries =
                        cons_.cached_prod_tail_ - cons_head;

                    if (LF_UNLIKELY(entries < n))
                        return 0;
                }

                cons_.head.store(
                    cons_head + static_cast<uint32_t>(n),
                    std::memory_order_relaxed);
            }
            else
            {
                if (LF_UNLIKELY(!mc_reserve(n, cons_head)))
                    return 0;
            }

            uint32_t idx = cons_head & mask_;

            uint32_t first =
                std::min<uint32_t>(
                    size_ - idx,
                    static_cast<uint32_t>(n));

            std::memcpy(
                out,
                &buffer_[idx],
                first * sizeof(T));

            if (first < n)
            {
                std::memcpy(
                    out + first,
                    buffer_,
                    (n - first) * sizeof(T));
            }

            if constexpr (!SingleConsumer)
            {
                while (cons_.tail.load(std::memory_order_relaxed) != cons_head)
                    cpu_relax();
            }

            cons_.tail.store(
                cons_head + static_cast<uint32_t>(n),
                std::memory_order_release);

            return n;
        }

    private:
        bool mp_reserve(std::size_t n, uint32_t &start) noexcept
        {
            while (true)
            {
                uint32_t head =
                    prod_.head.load(std::memory_order_relaxed);

                uint32_t cons_tail =
                    cons_.tail.load(std::memory_order_acquire);

                if (n > (size_ - (head - cons_tail)))
                    return false;

                if (prod_.head.compare_exchange_weak(
                        head,
                        head + static_cast<uint32_t>(n),
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
                {
                    start = head;
                    return true;
                }
            }
        }

        bool mc_reserve(std::size_t n, uint32_t &start) noexcept
        {
            while (true)
            {
                uint32_t head =
                    cons_.head.load(std::memory_order_relaxed);

                uint32_t prod_tail =
                    prod_.tail.load(std::memory_order_acquire);

                if (n > (prod_tail - head))
                    return false;

                if (cons_.head.compare_exchange_weak(
                        head,
                        head + static_cast<uint32_t>(n),
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
                {
                    start = head;
                    return true;
                }
            }
        }

        struct alignas(CACHELINE_SIZE) Prod
        {
            std::atomic<uint32_t> head{0};
            std::atomic<uint32_t> tail{0};

            uint32_t cached_cons_tail_{0};
            uint32_t pending_publish_{0};

        } prod_;

        struct alignas(CACHELINE_SIZE) Cons
        {
            std::atomic<uint32_t> head{0};
            std::atomic<uint32_t> tail{0};

            uint32_t cached_prod_tail_{0};

        } cons_;

        const uint32_t size_;
        const uint32_t mask_;
        const uint32_t publish_batch_;

        T *buffer_;
    };

}

#endif