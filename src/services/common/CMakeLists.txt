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
        include/network/publisher/BoostAsioTaskPublisher.h
        include/util/Exceptions.h
        include/network/connection/Connection.h include/network/connection/ConnectionManager.h src/network/connection/Connection.cpp)

include_directories(include)

set(BOOST_INCLUDEDIR "/usr/local/boost_1_66_0")
set(BOOST_LIBRARYDIR /usr/local/boost_1_66_0/stage/lib)

find_package(Boost 1.66.0 REQUIRED COMPONENTS system)
if (NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find boost!")
else ()
    message(STATUS "Services: Common: boost found, VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endif ()

add_executable(common-exec ${SOURCE_FILES} main.cpp)
target_link_libraries(common-exec ${Boost_LIBRARIES})
add_library(common ${SOURCE_FILES})
target_link_libraries(common ${Boost_LIBRARIES})
