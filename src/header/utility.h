#ifndef UTILITY
#define UTILITY

#include <iostream> //for iostream
#include <cstdlib> //for exit
#include <cstring> //for strtok_r
#include <stdio.h> //perror and printf
#include <sys/stat.h> //fstat
#include <linux/fs.h> //for BLKGETSIZE64
#include <sys/ioctl.h> //for ioctl
#include <unordered_map> //for unordered_map
#include <vector> //for vector
#include <unistd.h> //read
#include <fcntl.h> //open
#include <sys/types.h> //O_RDONLY

typedef struct stat stat_struct;

void fatal_error(std::string error_message); //fatal error helper function
uint64_t get_file_size(int file_fd); //gets file size of the file descriptor passed in
void sigint_handler(int sig_number); //handler used in main for handling SIGINT

//removes first n elements from a vector
template <typename T>
void remove_first_n_elements(std::vector<T> &data, int num_elements_to_remove){ //deals correctly with overlaps
  auto new_size = data.size() - num_elements_to_remove;
  std::memmove(&data[0], &data[num_elements_to_remove], new_size);
  data.resize(new_size);
}

template <typename T>
void remove_first_n_elements(T *data, int length, T *&ret_buff, int num_elements_to_remove){ //deals correctly with overlaps
  auto new_size = length - num_elements_to_remove;
  std::memmove(data, &data[num_elements_to_remove], new_size);
  char *temp_ptr = (T*)std::malloc(new_size);
  free(ret_buff);
  ret_buff = temp_ptr;
  std::memcpy(ret_buff, data, new_size);
}

#endif