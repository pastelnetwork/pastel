#include "ITask.h"

namespace nsfw {
    class FinishTask : public ITask {
    public:
        FinishTask(){}
        TaskType GetType() { return FinishWork; }
    };
}

