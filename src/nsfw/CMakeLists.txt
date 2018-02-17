cmake_minimum_required(VERSION 3.8)
project(client)

set(CMAKE_CXX_STANDARD 14)
set(BOOST_ROOT "/usr/local/boost_1_66_0/")

set(SOURCE_FILES main.cpp NSFWImageChecker.cpp NSFWImageChecker.h network/ITaskPublisher.h task/task/ITask.h task/task_result/ITaskResult.h ITaskScheduler.h util/AsynchronousQueue.h task/task/FinishTask.h task/task_result/InappropriateTaskResult.h task/task/TaskHeader.h task/task_result/AttemptsExhaustedResult.h task/task/NSFWImageTask.h util/Types.h network/protocol/IProtocol.h)
add_executable(client ${SOURCE_FILES})
include_directories(./)


find_package(Boost 1.66.0)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find boost!")
else()
    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
    message(STATUS "Boost_VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif()