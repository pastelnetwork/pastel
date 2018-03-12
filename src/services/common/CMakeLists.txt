project(common)

set(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_FLAGS -pthread)
set(BOOST_ROOT "/usr/local/boost_1_66_0/")

set(SOURCE_FILES
        include/dispatcher/TaskDispatcher.h
        src/dispatcher/ExecutorDispatcher.cpp
        include/dispatcher/ExecutorDispatcher.h
        include/network/protocol/IProtocol.h
        include/network/protocol/JSONProtocol.h
        src/util/univalue/univalue.cpp
        src/util/univalue/univalue_read.cpp
        src/util/univalue/univalue_write.cpp
        src/util/univalue/univalue_escapes.h
        src/util/univalue/univalue_utffilter.h
        src/util/utilstrencodings.cpp
        include/network/publisher/ITaskPublisher.h
        include/scheduler/ITaskScheduler.h
        include/task/task/common_tasks/FinishTask.h
        include/task/task/ITask.h
        include/task/task/TaskHeader.h
        include/task/task_result/common_task_results/AttemptsExhaustedResult.h
        include/task/task_result/common_task_results/InappropriateTaskResult.h
        include/task/task_result/ITaskResult.h
        include/util/AsynchronousQueue.h
        include/util/univalue.h
        include/util/tinyformat.h
        include/util/utilstrencodings.h
        include/util/Types.h
        include/scheduler/SchedulerFactory.h
        include/consts/Enums.h
        )

include_directories(include)

add_executable(common-exec ${SOURCE_FILES} main.cpp)
add_library(common ${SOURCE_FILES})

find_package(Boost 1.66.0)
if (NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find boost!")
else ()
    #    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
    #    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
    #    message(STATUS "Boost_VERSION: ${Boost_VERSION}")
    message(STATUS "Services: Common: boost found, VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif ()
