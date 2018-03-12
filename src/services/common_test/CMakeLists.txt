project(common_test)

SET(CMAKE_CXX_FLAGS -pthread)
set(TEST_SOURCE_FILES
        dispatcher/TestTaskDispatcher.cpp
        scheduler/test_TestTaskScheduler.cpp
        TestMain.cpp task/TestTask.h
        network/protocol/test_JSONProtocol.cpp
        task/TestTaskWithAdditionalField.h
        scheduler/TestTaskScheduler.h
        network/publisher/TestTaskPublisher.h
        network/publisher/test_TestTaskPublisher.cpp)

include_directories(./)
include_directories(../common/include)

set(BOOST_ROOT "/usr/local/boost_1_66_0/")
find_package(Boost 1.66.0)
if (NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find boost!")
else ()
    message(STATUS "Services: CommonTest: boost found, VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif ()

add_executable(common_test ${TEST_SOURCE_FILES})
target_link_libraries(common_test ${Boost_LIBRARIES} common)

add_test(NAME CommonServicesTest COMMAND test-services)