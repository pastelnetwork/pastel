project(common_test)

set(TEST_SOURCE_FILES
        dispatcher/TestTaskDispatcher.cpp
        scheduler/TestTaskScheduler.cpp
        TestMain.cpp task/TestTask.h network/protocol/TestJSONProtocol.cpp task/TestTaskWithAdditionalField.h)

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