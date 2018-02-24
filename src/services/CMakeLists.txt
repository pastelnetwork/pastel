cmake_minimum_required(VERSION 3.8)
project(services)

set(CMAKE_CXX_STANDARD 14)

set(BOOST_ROOT "/usr/local/boost_1_66_0/")

set(SOURCE_FILES
        common/dispatcher/TaskDispatcher.h
        common/network/protocol/IProtocol.h
        common/network/protocol/JSONProtocol.h
        common/network/publisher/ITaskPublisher.h
        common/scheduler/ITaskScheduler.h
        common/task/task/common_tasks/FinishTask.h
        common/task/task/ITask.h
        common/task/task/TaskHeader.h
        common/task/task_result/common_task_results/AttemptsExhaustedResult.h
        common/task/task_result/common_task_results/InappropriateTaskResult.h
        common/task/task_result/ITaskResult.h
        common/util/AsynchronousQueue.h
#        common/util/testQueue.cpp
        common/util/Types.h common/scheduler/SchedulerFactory.h common/dispatcher/ExecutorDispatcher.cpp common/dispatcher/ExecutorDispatcher.h
        )

add_executable(services ${SOURCE_FILES})

include_directories(common)


find_package(Boost 1.66.0)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find boost!")
else()
    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
    message(STATUS "Boost_VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif()