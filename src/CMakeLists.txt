cmake_minimum_required(VERSION 3.10)

project(webserver)

set(CMAKE_CXX_STANDARD 11)

# set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
# set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

set( SOURCE_FILE_LIST ${SOURCE_FILES} ) # SOURCE_FILES is a space separated list of files passed in on the command line
separate_arguments(SOURCE_FILE_LIST) # the string list is made into an actual list

add_executable(webserver ${SOURCE_FILE_LIST}) # the list is passed here to actually set the source files
#SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_GLIBCXX_DEBUG=1")
target_link_libraries(webserver -luring -lcrypto -lwolfssl -lpthread)