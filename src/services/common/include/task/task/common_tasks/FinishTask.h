#include "task/task/ITask.h"

namespace services {
    class FinishTask : public ITask {
    public:
        FinishTask() {}

        TaskType GetType() { return TT_FinishWork; }

        std::unordered_map<std::string, std::vector<byte>> AdditionalFieldsToSerialize() override {
            return std::unordered_map<std::string, std::vector<byte>>();
        }

        bool ParseAdditionalFields(std::unordered_map<std::string, std::vector<byte>> map) override {
            return true;
        }
    };
}

