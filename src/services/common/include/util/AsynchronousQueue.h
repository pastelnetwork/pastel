#pragma once


#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

template<typename T>
class AsynchronousQueue {
public:

    bool PopNoWait(T &item) {
        bool result = false;
        std::lock_guard<std::mutex> mlock(mutex);
        if (!queue.empty()) {
            item = queue.front();
            queue.pop();
            result = true;
        }
        return result;
    }

    T Pop() {
        std::unique_lock<std::mutex> mlock(mutex);
        while (queue.empty()) {
            cond.wait(mlock);
        }
        auto val = queue.front();
        queue.pop();
        return val;
    }

    void Pop(T &item) {
        std::unique_lock<std::mutex> mlock(mutex);
        while (queue.empty()) {
            cond.wait(mlock);
        }
        item = queue.front();
        queue.pop();
    }

    void Push(const T &item) {
        std::lock_guard<std::mutex> mlock(mutex);
        queue.push(item);
        cond.notify_one();
    }

    bool Empty() {
        std::lock_guard<std::mutex> mlock(mutex);
        bool empty = queue.empty();
        return empty;
    }

    size_t Size() {
        std::lock_guard<std::mutex> mlock(mutex);
        size_t size = queue.size();
        return size;
    }

    AsynchronousQueue() = default;

    AsynchronousQueue(const AsynchronousQueue &) = delete;            // disable copying
    AsynchronousQueue &operator=(const AsynchronousQueue &) = delete; // disable assignment

private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cond;
};

