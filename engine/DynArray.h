// DynArray.h - our own dynamic (resizable) array. Replaces std::vector.
//
// Amortized O(1) push_back (capacity doubles when full).
// Random access O(1). Insert/erase in the middle O(n) because of shifting.
#pragma once
#include <cstddef>
#include <stdexcept>
#include <utility>

template <typename T>
class DynArray {
public:
    DynArray() : data_(nullptr), size_(0), cap_(0) {}

    DynArray(const DynArray& o) : data_(nullptr), size_(0), cap_(0) {
        reserve(o.size_);
        for (size_t i = 0; i < o.size_; ++i) data_[i] = o.data_[i];
        size_ = o.size_;
    }

    DynArray(DynArray&& o) noexcept : data_(o.data_), size_(o.size_), cap_(o.cap_) {
        o.data_ = nullptr;
        o.size_ = o.cap_ = 0;
    }

    DynArray& operator=(const DynArray& o) {
        if (this != &o) {
            DynArray tmp(o);
            swap(tmp);
        }
        return *this;
    }

    DynArray& operator=(DynArray&& o) noexcept {
        if (this != &o) {
            delete[] data_;
            data_ = o.data_;
            size_ = o.size_;
            cap_ = o.cap_;
            o.data_ = nullptr;
            o.size_ = o.cap_ = 0;
        }
        return *this;
    }

    ~DynArray() { delete[] data_; }

    // ---- capacity ----
    size_t size() const { return size_; }
    size_t capacity() const { return cap_; }
    bool empty() const { return size_ == 0; }

    void reserve(size_t newCap) {
        if (newCap <= cap_) return;
        T* fresh = new T[newCap];
        for (size_t i = 0; i < size_; ++i) fresh[i] = std::move(data_[i]);
        delete[] data_;
        data_ = fresh;
        cap_ = newCap;
    }

    // ---- element access ----
    T& operator[](size_t i) { return data_[i]; }             // unchecked, O(1)
    const T& operator[](size_t i) const { return data_[i]; }

    T& at(size_t i) {
        if (i >= size_) throw std::out_of_range("DynArray::at: index out of range");
        return data_[i];
    }
    const T& at(size_t i) const {
        if (i >= size_) throw std::out_of_range("DynArray::at: index out of range");
        return data_[i];
    }

    T& front() { return at(0); }
    T& back() {
        if (size_ == 0) throw std::out_of_range("DynArray::back on empty array");
        return data_[size_ - 1];
    }
    const T& back() const {
        if (size_ == 0) throw std::out_of_range("DynArray::back on empty array");
        return data_[size_ - 1];
    }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

    // ---- modifiers ----
    void push_back(const T& v) {
        if (size_ == cap_) {
            T copy = v;  // v might live inside our own buffer
            reserve(cap_ ? cap_ * 2 : 8);
            data_[size_++] = std::move(copy);
        } else {
            data_[size_++] = v;
        }
    }

    void pop_back() {
        if (size_ == 0) throw std::out_of_range("DynArray::pop_back on empty array");
        --size_;
    }

    // Insert v so that it ends up at position 'index'. O(n - index).
    void insert(size_t index, const T& v) {
        if (index > size_) throw std::out_of_range("DynArray::insert: index out of range");
        T copy = v;
        if (size_ == cap_) reserve(cap_ ? cap_ * 2 : 8);
        for (size_t i = size_; i > index; --i) data_[i] = std::move(data_[i - 1]);
        data_[index] = std::move(copy);
        ++size_;
    }

    // Erase elements in [first, last). O(n - first).
    void erase(size_t first, size_t last) {
        if (first > last || last > size_) throw std::out_of_range("DynArray::erase: bad range");
        size_t n = last - first;
        if (n == 0) return;
        for (size_t i = last; i < size_; ++i) data_[i - n] = std::move(data_[i]);
        size_ -= n;
    }

    void erase(size_t index) { erase(index, index + 1); }

    // Shrink to newSize elements (no-op if already smaller).
    void truncate(size_t newSize) {
        if (newSize < size_) size_ = newSize;
    }

    // Make the array exactly newSize long. Growing fills the new slots with copies of
    // 'fill'; shrinking drops the tail. O(newSize - size) (plus one reallocation).
    void resize(size_t newSize, const T& fill = T()) {
        if (newSize > size_) {
            T copy = fill;  // 'fill' might live inside our own buffer
            reserve(newSize);
            for (size_t i = size_; i < newSize; ++i) data_[i] = copy;
        }
        size_ = newSize;
    }
    void append(const DynArray& other) {
        if (this == &other) {
            DynArray copy(other);
            append(copy);
            return;
        }
        reserve(size_ + other.size_);
        for (size_t i = 0; i < other.size_; ++i) data_[size_ + i] = other.data_[i];
        size_ += other.size_;
    }

    void clear() { size_ = 0; }

    void swap(DynArray& o) noexcept {
        std::swap(data_, o.data_);
        std::swap(size_, o.size_);
        std::swap(cap_, o.cap_);
    }

private:
    T* data_;
    size_t size_;
    size_t cap_;
};
