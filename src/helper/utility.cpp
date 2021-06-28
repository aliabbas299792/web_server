#include "../header/utility.h"
#include "../header/web_server/web_server.h"

#include <thread>
#include <malloc.h>

void fatal_error(std::string error_message){
  perror(std::string("Fatal Error: " + error_message).c_str());
  exit(1);
}

uint64_t get_file_size(int file_fd){
  stat_struct file_stat;

  if(fstat(file_fd, &file_stat) < 0)
    fatal_error("file stat");
  
  if(S_ISBLK(file_stat.st_mode)){
    uint64_t size_bytes;
    if(ioctl(file_fd, BLKGETSIZE64, &size_bytes) != 0)
      fatal_error("file stat ioctl");
    
    return size_bytes;
  }else if(S_ISREG(file_stat.st_mode)){
    return file_stat.st_size;
  }

  return -1;
}

void sigint_handler(int sig_number){
  std::cout << "\nShutting down...\n";

  central_web_server::instance().kill_server(); // the program gracefully exits without needing to explicitly exit
}