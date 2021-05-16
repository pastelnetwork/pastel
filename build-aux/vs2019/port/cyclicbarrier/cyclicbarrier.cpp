#include <chrono>
#include "cyclicbarrier/cyclicbarrier.hpp"

cbar::cyclicbarrier::cyclicbarrier(uint32_t parties, callable* call) {
   this->parties = parties; 
   this->call = call;
   this->current_waits = 0;
}

void cbar::cyclicbarrier::await(uint64_t nanosecs) {
    std::unique_lock<std::mutex> lck(lock);
    if (current_waits < parties) {
        current_waits++;
    } 
    if (current_waits >= parties) {
        std::lock_guard<std::mutex> rstl(reset_lock);
        lck.unlock();
        cv.notify_all(); 
        if (call != 0) {
            call->run();
        }
        return;
    } else {
        if (nanosecs > 0) {
           cv.wait_for(lck, std::chrono::nanoseconds(nanosecs));
        } else {
           cv.wait(lck);
        }
    }
    lck.unlock();
}

void cbar::cyclicbarrier::reset() {
    lock.lock();
    std::lock_guard<std::mutex> rstl(reset_lock);
    lock.unlock();
    cv.notify_all();
    if (call != 0) {
        call->run();
    }
    current_waits = 0;
}

uint32_t cbar::cyclicbarrier::get_barrier_size() const {
    std::lock_guard<std::mutex> lck(const_cast<std::mutex&>(lock));
    return parties;
}

uint32_t cbar::cyclicbarrier::get_current_waiting() const {
    std::lock_guard<std::mutex> lck(const_cast<std::mutex&>(lock));
    return current_waits;
}

