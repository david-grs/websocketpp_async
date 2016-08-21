cmake_minimum_required(VERSION 2.8)

project(ws)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -std=c++14 -g -Wall -Wextra")

find_package(Boost REQUIRED COMPONENTS thread date_time system regex filesystem serialization iostreams program_options)
include_directories(${Boost_INCLUDE_DIRS})

include_directories("/home/dgrs/cpp/thirdparty/websocketpp")
add_executable(ws main.cpp)
target_link_libraries(ws ${Boost_LIBRARIES} pthread lua5.2)

