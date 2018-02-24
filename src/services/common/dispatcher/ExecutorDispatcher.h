#pragma once


#include <vector>
#include <bits/unique_ptr.h>

#include "scheduler/ITaskScheduler.h"

namespace services {
    class ExecutorDispatcher {
    public:
        const size_t MIN_THRESHOLD = 4;

        ExecutorDispatcher(size_t threshold, size_t maxExecutorsNumber) {
            this->maxExecutorsNumber = std::max(maxExecutorsNumber, 1ul);
            this->threshold = std::max(threshold, MIN_THRESHOLD);
        }


        AddTaskResult AddTask(const std::shared_ptr<ITask> &task) {
            auto index = ChooseExecutor();
            return executors[index].get()->AddTask(task);
        }


    private:
        size_t ChooseExecutor() {
            return 0;
        }

        size_t threshold;
        size_t maxExecutorsNumber;
        std::vector<std::unique_ptr<ITaskScheduler>> executors;
    };
}

