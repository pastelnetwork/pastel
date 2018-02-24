
#pragma once


#include "ITaskScheduler.h"

namespace services {
    class SchedulerFactory {
    public:
        SchedulerFactory(std::unique_ptr<ITaskScheduler> proto){

        }

        ITaskScheduler *MakeScheduler();



    private:
        std::unordered_map<std::string, ITaskScheduler>
    };
}

