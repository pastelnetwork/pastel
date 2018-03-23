#pragma once

#include "task/task_result/ITaskResult.h"

namespace services {
    class InappropriateTaskResult : public ITaskResult {
    public:
        InappropriateTaskResult(boost::uuids::uuid id) {
            this->id = id;
            status = TaskResultStatus::TRS_InappropriateTask;
        }
    };
}


