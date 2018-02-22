#include <task/task/ITask.h>

namespace services {
    class FinishTask : public ITask {
    public:
        FinishTask(){}
        TaskType GetType() { return FinishWork; }
    };
}

