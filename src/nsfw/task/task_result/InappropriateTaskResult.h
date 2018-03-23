
#pragma once

#include "ITaskResult.h"

namespace nsfw {
    class InappropriateTaskResult : public ITaskResult {
    public:
        InappropriateTaskResult(boost::uuids::uuid id) : ITaskResult(id) {
            status = TaskResultStatus::InappropriateTask;
        }
    };
}


