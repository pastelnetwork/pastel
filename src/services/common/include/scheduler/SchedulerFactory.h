
#pragma once


#include <string>
#include <util/Exceptions.h>
#include "ITaskScheduler.h"

namespace services {
    class FactoryException : public BaseException {
    public:
        FactoryException(std::string message) : BaseException(message) {}
    };

    class SchedulerFactory {
    public:
        SchedulerFactory(std::unique_ptr<ITaskScheduler> proto) {
            if (!proto.get())
                throw FactoryException("No ITaskScheduler object provided");
            prototype = std::move(proto);
        }

        ITaskScheduler* MakeScheduler() {
            return prototype->Clone();
        }


    protected:
        std::unique_ptr<ITaskScheduler> prototype;
//        std::unordered_map<std::string, ITaskScheduler>
    };
}

