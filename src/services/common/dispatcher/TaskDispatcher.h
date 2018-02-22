
#pragma once


namespace services {
    class TaskDispatcher {
        enum Result {
            Success,
            NotSupporting
        };

    public:
        Result AddTask();
    };
}

