
#pragma once


#include "ITaskScheduler.h"

namespace services {
    class SchedulerFactory {
    public:
        SchedulerFactory(std::unique_ptr<ITaskScheduler> proto){

        }

        ITaskScheduler *MakeScheduler(std::string key) {

        }

    private:
        std::unordered_map<std::string, ITaskScheduler>
    };
}

