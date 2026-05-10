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
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace lockfree
{
#if defined(__x86_64__) || defined(__i386__)
    inline void cpu_relax() noexcept
    {
        __builtin_ia32_pause();
    }
#else
    inline void cpu_relax() noexcept {}
#endif

#if defined(__GNUC__) || defined(__clang__)
#define LF_LIKELY(x) __builtin_expect(!!(x), 1)
#define LF_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LF_LIKELY(x) (x)
#define LF_UNLIKELY(x) (x)
#endif
#if defined(__GNUC__) || defined(__clang__)
#define LF_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define LF_ALWAYS_INLINE inline
#endif

    constexpr std::size_t CACHELINE_SIZE = 64;

    LF_ALWAYS_INLINE bool is_power_of_two(std::size_t x)
    {
        return x && ((x & (x - 1)) == 0);
    }

    template <typename T>
    class SPSCQueue
    {
    public:
        explicit SPSCQueue(std::size_t size)
            : size_(static_cast<uint32_t>(size)), mask_(static_cast<uint32_t>(size - 1))
        {
            if (!is_power_of_two(size))
                throw std::invalid_argument("size must be power of two");
            buffer_ = static_cast<T *>(std::aligned_alloc(CACHELINE_SIZE, sizeof(T) * size_));
            if (!buffer_)
                throw std::bad_alloc();
        }

        ~SPSCQueue() { std::free(buffer_); }

        LF_ALWAYS_INLINE bool enqueue(const T &item) noexcept
        {
            uint32_t head = prod_head_;
            uint32_t next = head + 1;

            if (LF_UNLIKELY(next - cached_cons_tail_ > mask_))
            {
                cached_cons_tail_ = cons_tail_.load(std::memory_order_acquire);
                if (LF_UNLIKELY(next - cached_cons_tail_ > mask_))
                    return false;
            }
            uint32_t idx = head & mask_;
            copy_item(&buffer_[idx], &item);

            prod_head_ = next;
            prod_tail_.store(next, std::memory_order_release);
            return true;
        }

        LF_ALWAYS_INLINE bool dequeue(T &out) noexcept
        {
            uint32_t head = cons_head_;
            if (LF_UNLIKELY(head == cached_prod_tail_))
            {
                cached_prod_tail_ = prod_tail_.load(std::memory_order_acquire);
                if (LF_UNLIKELY(head == cached_prod_tail_))
                    return false;
            }

            uint32_t idx = head & mask_;
            copy_item(&out, &buffer_[idx]);

            uint32_t next = head + 1;
            cons_head_ = next;
            cons_tail_.store(next, std::memory_order_release);
            return true;
        }

    private:
        LF_ALWAYS_INLINE void copy_item(T *dst, const T *src) noexcept
        {
#if defined(__AVX2__)
            if constexpr (sizeof(T) == 32)
            {
                _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst),
                                    _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src)));
            }
            else if constexpr (sizeof(T) == 64)
            {
                auto *d = reinterpret_cast<__m256i *>(dst);
                auto *s = reinterpret_cast<const __m256i *>(src);
                _mm256_storeu_si256(d, _mm256_loadu_si256(s));
                _mm256_storeu_si256(d + 1, _mm256_loadu_si256(s + 1));
            }
            else
            {
                *dst = *src;
            }
#else
            *dst = *src;
#endif
        }
        alignas(CACHELINE_SIZE) uint32_t prod_head_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> prod_tail_{0};
        alignas(CACHELINE_SIZE) uint32_t cons_head_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> cons_tail_{0};
        alignas(CACHELINE_SIZE) uint32_t cached_cons_tail_{0};
        alignas(CACHELINE_SIZE) uint32_t cached_prod_tail_{0};

        const uint32_t size_;
        const uint32_t mask_;
        alignas(CACHELINE_SIZE) T *buffer_;
    };

    template <typename T>
    class SPSCQueueBatch
    {
    public:
        explicit SPSCQueueBatch(std::size_t size, uint32_t publish_batch = 32)
            : size_(static_cast<uint32_t>(size)),
              mask_(static_cast<uint32_t>(size - 1)),
              publish_batch_(publish_batch)
        {
            // Align to 32 bytes for AVX instructions
            buffer_ = static_cast<T *>(std::aligned_alloc(32, sizeof(T) * size_));
        }

        ~SPSCQueueBatch() { std::free(buffer_); }

        std::size_t enqueue_batch(const T *items, std::size_t n) noexcept
        {
            uint32_t head = prod_head_;
            uint32_t available = size_ - (head - cached_cons_tail_);

            if (LF_UNLIKELY(n > available))
            {
                cached_cons_tail_ = cons_tail_.load(std::memory_order_acquire);
                available = size_ - (head - cached_cons_tail_);
                if (LF_UNLIKELY(n > available))
                    n = available;
            }

            if (LF_UNLIKELY(n == 0))
                return 0;

            uint32_t idx = head & mask_;
            uint32_t first = std::min<uint32_t>(size_ - idx, static_cast<uint32_t>(n));

            copy_n_nt(&buffer_[idx], items, first);
            if (first < n)
            {
                copy_n_nt(buffer_, items + first, n - first);
            }

#if defined(__AVX2__)
            if constexpr (sizeof(T) == 32 || sizeof(T) == 64)
            {
                _mm_sfence();
            }
#endif

            prod_head_ = head + static_cast<uint32_t>(n);
            pending_publish_ += static_cast<uint32_t>(n);

            if (pending_publish_ >= publish_batch_)
            {
                prod_tail_.store(prod_head_, std::memory_order_release);
                pending_publish_ = 0;
            }

            return n;
        }

        std::size_t dequeue_batch(T *out_items, std::size_t n) noexcept
        {
            uint32_t head = cons_head_;
            uint32_t available = cached_prod_tail_ - head;

            if (LF_UNLIKELY(n > available))
            {
                cached_prod_tail_ = prod_tail_.load(std::memory_order_acquire);
                available = cached_prod_tail_ - head;
                if (LF_UNLIKELY(n > available))
                    n = available;
            }

            if (LF_UNLIKELY(n == 0))
                return 0;

            uint32_t idx = head & mask_;
            uint32_t first = std::min<uint32_t>(size_ - idx, static_cast<uint32_t>(n));

            copy_n_simd(out_items, &buffer_[idx], first);
            if (first < n)
            {
                copy_n_simd(out_items + first, buffer_, n - first);
            }

            cons_head_ = head + static_cast<uint32_t>(n);
            cons_tail_.store(cons_head_, std::memory_order_release);

            return n;
        }

    private:
        LF_ALWAYS_INLINE void copy_n_nt(T *dst, const T *src, std::size_t n) noexcept
        {
#if defined(__AVX2__)
            if constexpr (sizeof(T) == 32)
            {
                for (size_t i = 0; i < n; ++i)
                {
                    _mm256_stream_si256(reinterpret_cast<__m256i *>(dst + i),
                                        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + i)));
                }
                return;
            }
#endif
            std::memcpy(dst, src, n * sizeof(T));
        }

        LF_ALWAYS_INLINE void copy_n_simd(T *dst, const T *src, std::size_t n) noexcept
        {
#if defined(__AVX2__)
            if constexpr (sizeof(T) == 32)
            {
                for (size_t i = 0; i < n; ++i)
                {
                    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + i),
                                        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + i)));
                }
                return;
            }
#endif
            std::memcpy(dst, src, n * sizeof(T));
        }

        alignas(64) uint32_t prod_head_{0};
        uint32_t cached_cons_tail_{0};
        uint32_t pending_publish_{0};
        alignas(64) std::atomic<uint32_t> prod_tail_{0};

        alignas(64) uint32_t cons_head_{0};
        uint32_t cached_prod_tail_{0};
        alignas(64) std::atomic<uint32_t> cons_tail_{0};

        const uint32_t size_;
        const uint32_t mask_;
        const uint32_t publish_batch_;
        T *buffer_;
    };
    template <typename T>
    class MPMCQueue
    {
    public:
        explicit MPMCQueue(std::size_t size)
            : size_(static_cast<uint32_t>(size)), mask_(static_cast<uint32_t>(size - 1))
        {
            buffer_ = static_cast<T *>(std::aligned_alloc(CACHELINE_SIZE, sizeof(T) * size_));
        }

        ~MPMCQueue() { std::free(buffer_); }

        LF_ALWAYS_INLINE bool enqueue(const T &item) noexcept
        {
            uint32_t head;
            while (true)
            {
                head = prod_head_atomic_.load(std::memory_order_relaxed);
                uint32_t cons_tail = cons_tail_.load(std::memory_order_acquire);
                if (1 > (size_ - (head - cons_tail)))
                    return false;

                if (prod_head_atomic_.compare_exchange_weak(head, head + 1,
                                                            std::memory_order_acq_rel, std::memory_order_relaxed))
                    break;
            }

            buffer_[head & mask_] = item;
            while (prod_tail_.load(std::memory_order_relaxed) != head)
                cpu_relax();
            prod_tail_.store(head + 1, std::memory_order_release);
            return true;
        }

        LF_ALWAYS_INLINE bool dequeue(T &out) noexcept
        {
            uint32_t head;
            while (true)
            {
                head = cons_head_atomic_.load(std::memory_order_relaxed);
                uint32_t prod_tail = prod_tail_.load(std::memory_order_acquire);
                if (1 > (prod_tail - head))
                    return false;

                if (cons_head_atomic_.compare_exchange_weak(head, head + 1,
                                                            std::memory_order_acq_rel, std::memory_order_relaxed))
                    break;
            }

            while (cons_tail_.load(std::memory_order_relaxed) != head)
                cpu_relax();
            out = buffer_[head & mask_];
            cons_tail_.store(head + 1, std::memory_order_release);
            return true;
        }

    private:
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> prod_head_atomic_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> prod_tail_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> cons_head_atomic_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> cons_tail_{0};

        const uint32_t size_;
        const uint32_t mask_;
        T *buffer_;
    };

    template <typename T>
    class MPMCQueueBatch
    {
    public:
        explicit MPMCQueueBatch(std::size_t size)
            : size_(static_cast<uint32_t>(size)), mask_(static_cast<uint32_t>(size - 1))
        {
            buffer_ = static_cast<T *>(std::aligned_alloc(CACHELINE_SIZE, sizeof(T) * size_));
        }

        ~MPMCQueueBatch() { std::free(buffer_); }

        inline std::size_t enqueue_batch(const T *items, std::size_t n) noexcept
        {
            uint32_t head;
            std::size_t actual_n;
            while (true)
            {
                head = prod_head_atomic_.load(std::memory_order_relaxed);
                uint32_t cons_tail = cons_tail_.load(std::memory_order_acquire);
                uint32_t available = size_ - (head - cons_tail);
                actual_n = (n > available) ? available : n;
                if (actual_n == 0)
                    return 0;

                if (prod_head_atomic_.compare_exchange_weak(head, head + actual_n,
                                                            std::memory_order_acq_rel, std::memory_order_relaxed))
                    break;
            }

            for (std::size_t i = 0; i < actual_n; ++i)
            {
                buffer_[(head + i) & mask_] = items[i];
            }

            while (prod_tail_.load(std::memory_order_relaxed) != head)
                cpu_relax();
            prod_tail_.store(head + actual_n, std::memory_order_release);
            return actual_n;
        }

        inline std::size_t dequeue_batch(T *out_items, std::size_t n) noexcept
        {
            uint32_t head;
            std::size_t actual_n;
            while (true)
            {
                head = cons_head_atomic_.load(std::memory_order_relaxed);
                uint32_t prod_tail = prod_tail_.load(std::memory_order_acquire);
                uint32_t available = prod_tail - head;
                actual_n = (n > available) ? available : n;
                if (actual_n == 0)
                    return 0;

                if (cons_head_atomic_.compare_exchange_weak(head, head + actual_n,
                                                            std::memory_order_acq_rel, std::memory_order_relaxed))
                    break;
            }

            while (cons_tail_.load(std::memory_order_relaxed) != head)
                cpu_relax();
            for (std::size_t i = 0; i < actual_n; ++i)
            {
                out_items[i] = buffer_[(head + i) & mask_];
            }
            cons_tail_.store(head + actual_n, std::memory_order_release);
            return actual_n;
        }

    private:
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> prod_head_atomic_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> prod_tail_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> cons_head_atomic_{0};
        alignas(CACHELINE_SIZE) std::atomic<uint32_t> cons_tail_{0};

        const uint32_t size_;
        const uint32_t mask_;
        T *buffer_;
    };

} // namespace lockfree

#endif