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
        TRS_AllAttemptsExhausted,
        TRS_Last
    };

    class ITaskResult {
    public:
        TaskResultStatus GetStatus() const { return status; }

        const boost::uuids::uuid &GetId() const { return id; }

        const std::string &GetResult() const {
            return result;
        }

        const std::string &GetMessage() const {
            return message;
        }

        void SetId(const boost::uuids::uuid &id) {
            ITaskResult::id = id;
        }

        void SetStatus(TaskResultStatus status) {
            ITaskResult::status = status;
        }

        void SetResult(const std::string &result) {
            ITaskResult::result = result;
        }

        void SetMessage(const std::string &message) {
            ITaskResult::message = message;
        }

    protected:
        boost::uuids::uuid id;
        TaskResultStatus status;
        std::string result;
        std::string message;
    };

    typedef std::function<void(ITaskResult)> ResponseCallback;
}

