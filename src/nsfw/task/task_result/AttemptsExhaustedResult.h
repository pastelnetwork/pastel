//
// Created by artem on 11.02.18.
//

#pragma once


#include "ITaskResult.h"

namespace nsfw {
    class AttemptsExhaustedResult : public ITaskResult {
    public:
        AttemptsExhaustedResult(boost::uuids::uuid id) : ITaskResult(id) {
            status = TaskResultStatus::AllAttemptsExhausted;
        }
    };
}

