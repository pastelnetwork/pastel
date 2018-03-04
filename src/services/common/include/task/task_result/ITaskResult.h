//
// Created by artem on 10.02.18.
//

#pragma once

#include <boost/uuid/uuid.hpp>
#include <functional>


namespace services {
    enum TaskResultStatus {
        TRS_Success,
        TRS_InappropriateTask,
        TRS_AllAttemptsExhausted
    };

    class ITaskResult {
    public:
        ITaskResult(const boost::uuids::uuid &id) : id(id) {}

        TaskResultStatus GetStatus() const { return status; }

        const boost::uuids::uuid &GetId() const { return id; }

    protected:
        boost::uuids::uuid id;
        TaskResultStatus status;
    };

    typedef std::function<void(ITaskResult)> ResponseCallback;
}

