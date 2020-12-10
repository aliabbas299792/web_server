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

void read_file_and_prep_iovecs(std::string filepath, iovec *&iovecs, int &iovec_count, int num_prepend_iovecs = 0){
  int file_fd = open(filepath.c_str(), O_RDONLY);

  if(file_fd > 0){
    auto size = get_file_size(file_fd);
    iovec_count = size / READ_BLOCK_SIZE;
    if(size % READ_BLOCK_SIZE) iovec_count++;
    iovecs = (iovec*)std::malloc(sizeof(iovec) * (iovec_count + num_prepend_iovecs));
    for(int i = num_prepend_iovecs; i < iovec_count + num_prepend_iovecs; i++){
      int size_this_time = size - READ_BLOCK_SIZE > 0 ? READ_BLOCK_SIZE : size;
      iovecs[i].iov_base = std::malloc(size_this_time);
      iovecs[i].iov_len = size_this_time;
      size -= READ_BLOCK_SIZE;
    }
    readv(file_fd, &iovecs[num_prepend_iovecs], iovec_count);
  }else{
    iovec_count = -1;
  }
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

    int prepend_iovecs = 1; //how many iovecs to prepend
    iovec *iovecs_write = nullptr;
    int iovec_count_write = 0;
    read_file_and_prep_iovecs(processed_path, iovecs_write, iovec_count_write, prepend_iovecs);

    if(iovec_count_write > 0){
      //the header iovec to prepend, and only prepend if the iovecs have been allocated
      char header[] = "HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n";
      iovecs_write[0].iov_base = std::malloc(strlen(header));
      iovecs_write[0].iov_len = strlen(header);
      std::memcpy(iovecs_write[0].iov_base, header, strlen(header));

      /*for(int i = 0; i < iovec_count_write + prepend_iovecs; i++){
        for(int j = 0; j < iovecs_write[i].iov_len; j++){
          fputc(((char*)(iovecs_write[i].iov_base))[j], stdout);
        }
      }*/

      web_server->add_write_req(client_fd, iovecs_write, iovec_count_write + prepend_iovecs); //pass the data to the write function
    }
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