// Allocation and output primitives for mangly.
//
// Deliberately cstdlib-only: no <string>, <vector>, <memory>, or exceptions.
// The AST is bump-allocated from an Arena (raw pointers, no per-node malloc, no
// atomic refcounts); text is built into an OutputBuffer that grows with
// realloc. This keeps object-code emission lean and predictable and avoids the
// general-purpose-allocator / throwing STL the project's philosophy rules out.
#ifndef MANGLY_SUPPORT_HPP
#define MANGLY_SUPPORT_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace mangly {

// A non-owning view of bytes (typically into the input mangled name, which must
// outlive any AST derived from it).
struct StringView {
    const char* data = nullptr;
    std::uint32_t size = 0;
};

inline StringView make_sv(const char* z) {
    return StringView{z, static_cast<std::uint32_t>(std::strlen(z))};
}

inline bool sv_equal(StringView a, StringView b) {
    return a.size == b.size && std::memcmp(a.data, b.data, a.size) == 0;
}

// Bump allocator: a singly linked list of malloc'd blocks. Objects are never
// individually freed, so handed-out pointers stay valid for the arena's life;
// reset() rewinds every block for reuse across many demangles without freeing.
class Arena {
public:
    Arena() = default;
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    ~Arena() { free_all(); }

    void* allocate(std::size_t bytes, std::size_t align) {
        for (;;) {
            if (head_) {
                std::size_t base = reinterpret_cast<std::size_t>(head_->data());
                std::size_t cur = base + head_->used;
                std::size_t aligned = (cur + (align - 1)) & ~(align - 1);
                std::size_t off = aligned - base;
                if (off + bytes <= head_->cap) {
                    head_->used = off + bytes;
                    return reinterpret_cast<void*>(aligned);
                }
            }
            if (!new_block(bytes + align)) {
                return nullptr;  // OOM: caller propagates failure
            }
        }
    }

    template <class T>
    T* alloc(std::uint32_t count = 1) {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    void reset() {
        for (Block* b = head_; b; b = b->next) {
            b->used = 0;
        }
    }

private:
    struct Block {
        Block* next;
        std::size_t cap;
        std::size_t used;
        char* data() { return reinterpret_cast<char*>(this + 1); }
    };

    static constexpr std::size_t kDefault = 4096;

    Block* new_block(std::size_t need) {
        std::size_t cap = need > kDefault ? need : kDefault;
        Block* b = static_cast<Block*>(std::malloc(sizeof(Block) + cap));
        if (!b) return nullptr;
        b->next = head_;
        b->cap = cap;
        b->used = 0;
        head_ = b;
        return b;
    }

    void free_all() {
        Block* b = head_;
        while (b) {
            Block* n = b->next;
            std::free(b);
            b = n;
        }
        head_ = nullptr;
    }

    Block* head_ = nullptr;
};

// Growable byte buffer for rendered text. All appends are no-throw; on OOM the
// buffer latches a failure flag and drops further writes.
class OutputBuffer {
public:
    OutputBuffer() = default;
    OutputBuffer(const OutputBuffer&) = delete;
    OutputBuffer& operator=(const OutputBuffer&) = delete;
    ~OutputBuffer() { std::free(data_); }

    void push(char c) {
        if (!reserve(1)) return;
        data_[size_++] = c;
    }

    void append(const char* s, std::size_t n) {
        if (!reserve(n)) return;
        std::memcpy(data_ + size_, s, n);
        size_ += n;
    }

    void append(const char* z) { append(z, std::strlen(z)); }
    void append(StringView sv) { append(sv.data, sv.size); }

    void append_uint(std::uint64_t v) {
        char tmp[20];
        int i = 0;
        if (v == 0) tmp[i++] = '0';
        while (v) {
            tmp[i++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        }
        if (!reserve(static_cast<std::size_t>(i))) return;
        while (i) data_[size_++] = tmp[--i];
    }

    char back() const { return size_ ? data_[size_ - 1] : '\0'; }
    const char* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool failed() const { return failed_; }
    void clear() { size_ = 0; failed_ = false; }

    // NUL-terminated view of the contents (valid until the next mutation).
    const char* c_str() {
        if (!reserve(0)) return "";
        data_[size_] = '\0';
        return data_;
    }

    // Mutable view of the contents; useful for in-place tokenizing after a read.
    char* mutable_data() { return data_; }

private:
    bool reserve(std::size_t extra) {
        if (failed_) return false;
        std::size_t need = size_ + extra + 1;  // +1 for a possible NUL
        if (need <= cap_) return true;
        std::size_t nc = cap_ ? cap_ * 2 : 64;
        while (nc < need) nc *= 2;
        char* nd = static_cast<char*>(std::realloc(data_, nc));
        if (!nd) {
            failed_ = true;
            return false;
        }
        data_ = nd;
        cap_ = nc;
        return true;
    }

    char* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cap_ = 0;
    bool failed_ = false;
};

// Minimal growable array of trivially-copyable T (used for pointer lists and the
// substitution tables). realloc-backed; push is no-throw and reports OOM.
template <class T>
class Vec {
public:
    Vec() = default;
    Vec(const Vec&) = delete;
    Vec& operator=(const Vec&) = delete;
    ~Vec() { std::free(data_); }

    bool push(T v) {
        if (size_ == cap_) {
            std::uint32_t nc = cap_ ? cap_ * 2 : 8;
            T* nd = static_cast<T*>(std::realloc(data_, sizeof(T) * nc));
            if (!nd) {
                failed_ = true;
                return false;
            }
            data_ = nd;
            cap_ = nc;
        }
        data_[size_++] = v;
        return true;
    }

    std::uint32_t size() const { return size_; }
    T operator[](std::uint32_t i) const { return data_[i]; }
    const T* data() const { return data_; }
    void clear() { size_ = 0; }
    bool failed() const { return failed_; }

private:
    T* data_ = nullptr;
    std::uint32_t size_ = 0;
    std::uint32_t cap_ = 0;
    bool failed_ = false;
};

}  // namespace mangly

#endif  // MANGLY_SUPPORT_HPP
