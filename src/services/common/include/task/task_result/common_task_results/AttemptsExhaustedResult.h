//
// Created by artem on 11.02.18.
//

#pragma once


#include "task/task_result/ITaskResult.h"

namespace services {
    class AttemptsExhaustedResult : public ITaskResult {
    public:
        AttemptsExhaustedResult(boost::uuids::uuid id) {
            this->id = id;
            status = TaskResultStatus::TRS_AllAttemptsExhausted;
        }
    };
}

