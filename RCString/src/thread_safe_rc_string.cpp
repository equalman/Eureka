/*
 @ 0xCCCCCCCC
*/

#include "thread_safe_rc_string.h"

#include <cassert>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>

constexpr size_t kBaseSize = 4;

// Rounds up `num` to the nearest multiple of `factor`.
constexpr size_t RoundToMultiple(size_t num, size_t factor)
{
    return factor == 0 ? 0 : (num - 1 - (num - 1) % factor + factor);
}

class ThreadSafeStringData {
public:
    explicit ThreadSafeStringData(size_t capacity)
        : size_(0), ref_count_(1)
    {
        Reserve(capacity);
    }

    ~ThreadSafeStringData()
    {
        auto ref_count = ref_count_.load();
        assert(ref_count == 0 || ref_count == kUnsharedRefMark);
        std::cout << "[D]: releasing StringData\n";
    }

    ThreadSafeStringData(const ThreadSafeStringData&) = delete;

    ThreadSafeStringData& operator=(const ThreadSafeStringData&) = delete;

    ThreadSafeStringData* Clone(size_t new_capacity) const;

    void CopyData(const char* str, size_t length, size_t pos) noexcept;

    // Ensures enought capacity (at least in size of `capacity`) for string content.
    // Note that this function will never change the content.
    void Reserve(size_t capacity);

    char* data() const noexcept
    {
        return buffer_.get();
    }

    size_t size() const noexcept
    {
        return size_;
    }

    // If it returns true, then the uniqueness is held until the next call that involves sharing;
    // However, if it returns false, we can't guarantee it's not unique once the call ends.
    bool Unique() const noexcept
    {
        auto ref_count = ref_count_.load();
        return ref_count == 1 || ref_count == kUnsharedRefMark;
    }

    bool Unsharedable() const noexcept
    {
        return ref_count_.load() == kUnsharedRefMark;
    }

    // If the data is unsharedable, no more owner can share this resource.
    void MakeUnsharedable() const noexcept
    {
        assert(Unique());
        ref_count_.store(kUnsharedRefMark);
    }

    void ResetSharedable() const noexcept
    {
        assert(Unique());
        ref_count_.store(1);
    }

    void AddRef() const noexcept
    {
        assert(ref_count_.load() > 0);
        ref_count_.fetch_add(1);
    }

    // The last owner should cleanup the data when this function returns true.
    bool Release() const noexcept
    {
        assert(ref_count_.load() > 0);
        if (Unsharedable() || ref_count_.fetch_sub(1) == 0) {
            return true;
        }

        return false;
    }

private:
    std::unique_ptr<char[]> buffer_;
    size_t capacity_;
    size_t size_;
    mutable std::atomic<unsigned int> ref_count_;
    static constexpr size_t kUnsharedRefMark = static_cast<unsigned int>(-1);
};

void ThreadSafeStringData::Reserve(size_t required_capacity)
{
    if (required_capacity <= capacity_) {
        return;
    }

    auto new_base = std::max(capacity_ * 3 / 2, required_capacity);
    auto new_capacity = RoundToMultiple(new_base, kBaseSize);
    char* new_buf = new char[new_capacity];
    memcpy_s(new_buf, new_capacity, buffer_.get(), size_);
    buffer_.reset(new_buf);
    capacity_ = new_capacity;
}

void ThreadSafeStringData::CopyData(const char* str, size_t length, size_t pos) noexcept
{
    assert(Unique());
    assert(pos <= size_);
    assert(pos + length <= capacity_);
    memcpy_s(buffer_.get() + pos, capacity_, str, length);
    size_ = std::max(pos + length, size_);
}

ThreadSafeStringData* ThreadSafeStringData::Clone(size_t new_capacity) const
{
    auto* data = new ThreadSafeStringData(std::max(new_capacity, capacity_));
    data->CopyData(buffer_.get(), size_, 0);
    return data;
}