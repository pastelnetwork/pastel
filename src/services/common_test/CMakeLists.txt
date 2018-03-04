project(common_test)

set(TEST_SOURCE_FILES
        dispatcher/TestTaskDispatcher.h.cpp
        scheduler/TestTaskScheduler.cpp
        TestMain.cpp)

include_directories(./)

set(BOOST_ROOT "/usr/local/boost_1_66_0/")
find_package(Boost 1.66.0)
if (NOT Boost_FOUND)
    message(FATAL_ERROR "Could not find boost!")
else ()
    #    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
    #    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
    #    message(STATUS "Boost_VERSION: ${Boost_VERSION}")
    message(STATUS "Services: CommonTest: boost found, VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif ()

add_executable(common_test ${TEST_SOURCE_FILES})
