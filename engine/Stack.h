// Stack.h - our own LIFO stack, built on top of DynArray. Replaces std::stack.
//
// push / pop / top are all O(1) (push is amortized).
// This is the structure behind Undo/Redo (phase 2) and bracket matching (phase 4).
#pragma once
#include <stdexcept>
#include "DynArray.h"

template <typename T>
class Stack {
public:
    void push(const T& v) { items_.push_back(v); }

    T pop() {
        if (items_.empty()) throw std::underflow_error("Stack::pop on empty stack");
        T v = items_[items_.size() - 1];
        items_.pop_back();
        return v;
    }

    T& top() {
        if (items_.empty()) throw std::underflow_error("Stack::top on empty stack");
        return items_[items_.size() - 1];
    }
    const T& top() const {
        if (items_.empty()) throw std::underflow_error("Stack::top on empty stack");
        return items_[items_.size() - 1];
    }

    // Keep only the newest 'maxSize' items, dropping the oldest from the bottom.
    // Used to cap the undo history. O(size) but only runs when the cap is hit.
    void trim(size_t maxSize) {
        if (items_.size() > maxSize) items_.erase(0, items_.size() - maxSize);
    }

    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    void clear() { items_.clear(); }

private:
    DynArray<T> items_;
};
