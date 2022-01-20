#pragma once
#include <cinttypes>
#include <stdint.h>
#include <mutex>
#include <condition_variable>

namespace cbar 
{
    class callable 
    {
    public:
        virtual void run() = 0;
    };

    class cyclicbarrier {
    public:
        /*! Constructor
          \param parties how many callers has to wait before all of them are waked up
          \param call , callable object which is fired once waiters are signaled
        */
        cyclicbarrier(uint32_t parties, callable* call=0);

        /*!
          reset makes the cyclicbarrier object reusable, also wakes up any threads 
          waiting on it  
        */  
        void reset();

        /*!
            await is called to await for a number for required number of parties to wait 
            upon the barrier object. 
          \param nanosecs is waittime in nanoseconds, by default it is zero which specifies 
           indefinite wait  
        */
        void await(uint64_t nanosecs=0);

        /*!
          get_barrier_size returns the number of parties required to wait upon the object, before
          the waiters are woke up 
        */
        uint32_t get_barrier_size() const;

        /*!
            get_current_waiting returns how many threads are currently waiting on the object
        */
        uint32_t get_current_waiting() const;

    private:
        std::condition_variable cv;
        std::mutex lock;
        std::mutex reset_lock;
        uint32_t parties;
        uint32_t current_waits;
        callable *call;
        
        // deleted constructors/assignmenet operators
        cyclicbarrier() = delete;
        cyclicbarrier(const cyclicbarrier& other) = delete;
        cyclicbarrier& operator=(const cyclicbarrier& opther) = delete;
    };
}
