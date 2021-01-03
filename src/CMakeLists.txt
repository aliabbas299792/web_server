cmake_minimum_required(VERSION 3.10)

project(Liburing_Server)

set(CMAKE_CXX_STANDARD 11)

#this recursively gets all .cpp and .h files
file(GLOB_RECURSE sourceFiles "*.cpp" "*.h")

add_executable(Liburing_Server ${sourceFiles})

target_link_libraries(Liburing_Server -luring -lcrypto)