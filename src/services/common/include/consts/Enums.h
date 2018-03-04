#pragma once
namespace services {
    enum AddTaskResult {
        ATR_Success,
        ATR_NotSupporting,
        ATR_DispatcherIsMutable, // need finish initializing of dispatcher and make it immutable
        ATR_UnknownTaskType,
        ATR_ResponseCallbackNotSet,
        ATR_NoAvailableExecutor
    };

    enum SendResult {
        SendResult_Success,
        SendResult_ProtocolError,
    };

}