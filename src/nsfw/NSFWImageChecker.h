
#pragma once

#include "ITaskScheduler.h"

namespace nsfw {
    class NSFWImageChecker : public ITaskScheduler {
    public:
    protected:
        bool IsAppropriateTask(std::shared_ptr<ITask> &task) override;

        void HandleTask(std::shared_ptr<ITask> &task) override;
    };
}