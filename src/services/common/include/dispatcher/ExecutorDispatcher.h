#pragma once


#include <vector>
#include <bits/unique_ptr.h>
#include "scheduler/SchedulerFactory.h"


namespace services {
    class ExecutorDispatcher {
    public:
        const size_t MIN_THRESHOLD = 4;

        ExecutorDispatcher(size_t threshold, size_t maxExecutorsNumber, std::unique_ptr<SchedulerFactory> factory) {
            this->maxExecutorsNumber = std::max(maxExecutorsNumber, 1ul);
            this->threshold = std::max(threshold, MIN_THRESHOLD);
            this->factory = std::move(factory);
        }


        AddTaskResult AddTask(const std::shared_ptr<ITask> &task) {
            auto executor = ChooseExecutor();
            if (executor.get())
                return executor->AddTask(task);
            else
                return AddTaskResult::ATR_NoAvailableExecutor;
        }


    private:
        std::shared_ptr<ITaskScheduler> ChooseExecutor() {
            std::lock_guard<std::mutex> mlock(executorsMutex);
            if (executors.empty()) {
                return AddNewExecutor();
            } else {
                std::shared_ptr<ITaskScheduler> selectedExecutor;
                size_t minTasksCount = UINTMAX_MAX;
                size_t currentTasksCount;
                for (auto executor : executors) {
                    currentTasksCount = executor->TasksCount();
                    if (currentTasksCount < minTasksCount) {
                        minTasksCount = currentTasksCount;
                        selectedExecutor = executor;
                    }
                }
                if ((minTasksCount > threshold) && (executors.size() < maxExecutorsNumber)) {
                    selectedExecutor = AddNewExecutor();
                }
                return selectedExecutor;
            }


        }

        std::shared_ptr<ITaskScheduler> AddNewExecutor() {
            std::shared_ptr<ITaskScheduler> newExecutor = std::shared_ptr<ITaskScheduler>(factory->MakeScheduler());
            newExecutor->Run();
            executors.push_back(newExecutor);
            return newExecutor;
        }

        size_t threshold;
        size_t maxExecutorsNumber;
        std::mutex executorsMutex;
        std::vector<std::shared_ptr<ITaskScheduler>> executors;
        std::unique_ptr<SchedulerFactory> factory;
    };
}

