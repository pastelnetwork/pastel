cmake_minimum_required(VERSION 3.8)
project(services)

set(CMAKE_CXX_STANDARD 14)
enable_testing()

add_subdirectory(common)
add_subdirectory(common_test)

#set(BOOST_ROOT "/usr/local/boost_1_66_0/")
#find_package(Boost 1.66.0)
#if(NOT Boost_FOUND)
#    message(FATAL_ERROR "Could not find boost!")
#else()
#    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
#    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
#    message(STATUS "Boost_VERSION: ${Boost_VERSION}")
#    include_directories(${Boost_INCLUDE_DIRS})
#endif()