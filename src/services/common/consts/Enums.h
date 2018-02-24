#pragma once
enum AddTaskResult {
    Success,
    NotSupporting,
    DispatcherIsMutable, // need finish initializing of dispatcher and make it immutable
    UnknownTaskType,
    ResponseCallbackNotSet
};