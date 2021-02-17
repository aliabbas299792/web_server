cmake_minimum_required(VERSION 3.10)

project(Liburing_Server)

set(CMAKE_CXX_STANDARD 11)

#this recursively gets all .cpp and .h files
file(GLOB_RECURSE sourceFiles "*.cpp" "*.h" "*.tcc")

# set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
# set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

add_executable(Liburing_Server ${sourceFiles})
#SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_GLIBCXX_DEBUG=1")
target_link_libraries(Liburing_Server -luring -lcrypto -lwolfssl -lpthread)