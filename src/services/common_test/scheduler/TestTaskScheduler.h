
#pragma once


#include <scheduler/ITaskScheduler.h>

namespace services {
    class TestTaskScheduler : public ITaskScheduler {

    public:
        TestTaskScheduler(std::unique_ptr<ITaskPublisher> publisher) : ITaskScheduler(std::move(publisher)) {}

//        ITaskScheduler *Clone() override {
//            return new TestTaskScheduler(*this);
//        }

    protected:
        bool IsAppropriateTask(std::shared_ptr<ITask> &task) override {
            return task->GetType() == TaskType::TT_Test;
        }

        void HandleTask(std::shared_ptr<ITask> &task) override {
            if (publisher) {
                publisher->Send(task);
            }
        }
    };
}

