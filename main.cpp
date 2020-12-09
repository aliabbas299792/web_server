#include "server.h"

#include <unistd.h> //read
#include <sys/stat.h> //fstat
#include <fcntl.h> //open
#include <sys/types.h> //O_RDONLY

typedef struct stat stat_struct;

void sigint_handler(int sig_number){
  std::cout << " Shutting down...\n";
  exit(0);
}

void accept_callback(int client_fd, server *web_server){
  //std::cout << "accepted client with client_fd: " << client_fd << "\n";
}

int get_file_size(int file_fd){
  stat_struct file_stat;

  if(fstat(file_fd, &file_stat) < 0)
    fatal_error("file stat");
  
  if(S_ISBLK(file_stat.st_mode)){
    uint long long size_bytes;
    if(ioctl(file_fd, BLKGETSIZE64, &size_bytes) != 0)
      fatal_error("file stat ioctl");
    
    return size_bytes;
  }else if(S_ISREG(file_stat.st_mode)){
    return file_stat.st_size;
  }

  return -1;
}

void read_file(std::string filepath, char *buffer, int &length){
  int file_fd = open(filepath.c_str(), O_RDONLY);
  auto size = get_file_size(file_fd);
  std::cout << filepath << "\n";

}

void read_callback(int client_fd, int iovec_count, iovec iovecs[], server *web_server){
  std::vector<std::string> headers;
  
  for(int i = 0; i < iovec_count; i++){
    char *str = nullptr;
    while((str = strtok(((char*)iovecs[i].iov_base), "\r\n"))){ //retrieves the headers
      headers.push_back(std::string(str, strlen(str)));
      iovecs[i].iov_base = nullptr;
    }
  }
  
  if(!strcmp(strtok((char*)headers[0].c_str(), " "), "GET")){
    char *path = strtok(nullptr, " ");
    std::string processed_path = std::string(&path[1], strlen(path)-1);
    processed_path = processed_path == "" ? "index.html" : processed_path;

    char *http_version = strtok(nullptr, " ");

    char *data = nullptr;
    int length = 0;
    read_file(processed_path, data, length);
    
    auto write_data = malloc(length);
    std::memcpy(write_data, data, length);
    web_server->add_write_req(client_fd, write_data, length); //pass the data to the write function
    close(client_fd); //for web requests you close the socket right after
  }
}

void write_callback(int client_fd, server *web_server){

}

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed

  server web_server(accept_callback, read_callback, write_callback);

  return 0;
}