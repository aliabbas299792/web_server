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

std::string getContentType(std::string filepath){
  char *file_extension_data = (char*)filepath.c_str();
  std::string file_extension = "";
  while((file_extension_data = strtok(file_extension_data, "."))){
    file_extension = file_extension_data;
    file_extension_data = nullptr;
  }

  if(file_extension == "html" || file_extension == "htm")
    return "Content-Type: text/html\r\n";
  if(file_extension == "css")
    return "Content-Type: text/css\r\n";
  if(file_extension == "js")
    return "Content-Type: text/javascript\r\n";
  if(file_extension == "opus")
    return "Content-Type: audio/opus\r\n";
  if(file_extension == "mp3")
    return "Content-Type: audio/mpeg\r\n";
  if(file_extension == "mp4")
    return "Content-Type: video/mp4\r\n";
  if(file_extension == "gif")
    return "Content-Type: image/gif\r\n";
  if(file_extension == "png")
    return "Content-Type: image/png\r\n";
  if(file_extension == "jpg" || file_extension == "jpeg")
    return "Content-Type: image/jpeg\r\n";
  if(file_extension == "txt")
    return "Content-Type: text/plain\r\n";
  return "Content-Type: application/octet-stream\r\n";
}

std::string to_hex(int num){
  std::string str = "";
  for(char i = 0; i < 8; i++){
      char numChar = (num >> 4*i) & 0xf;
      str += (char)(numChar < 10 ? numChar + 48 : numChar - 10 + 65);
  }
  std::string retStr = "";
  for(char i = str.size() - 1; i >= 0; i--) retStr += str[i];
  retStr.erase(0, std::min(retStr.find_first_not_of('0'), str.size()-1));
  return retStr;
}

int read_file_and_prep_iovecs(std::string filepath, iovec *&iovecs, int &iovec_count, int num_prepend_iovecs = 0){
  int file_fd = open(filepath.c_str(), O_RDONLY);
  const int read_size = READ_BLOCK_SIZE - 12; //12 bytes for chunked encoding data
  
  if(file_fd < 0) return -1;
  auto original_size = get_file_size(file_fd);
  auto size = original_size;

  char *blockStr = (char*)(to_hex(read_size) + "\r\n").c_str();
  char *finalStr = (char*)(to_hex(size % read_size) + "\r\n").c_str();
  char endStr[] = {'\r', '\n'};
  char endMsgStr[] = "0\r\n\r\n";

  iovec_count = size / read_size;
  if(size % read_size) iovec_count++;
  iovec_count++; //extra empty iovec
  iovecs = (iovec*)std::malloc(sizeof(iovec) * (iovec_count + num_prepend_iovecs));

  for(int i = num_prepend_iovecs; i < iovec_count + num_prepend_iovecs; i++){
    if(size - read_size >= read_size){
      iovecs[i].iov_base = (char*)(std::malloc(read_size)) + 10;
      iovecs[i].iov_len = read_size;
      std::memcpy((char*)(iovecs[i].iov_base) - 10, blockStr, strlen(blockStr));
      std::memcpy((char*)(iovecs[i].iov_base) + read_size, endStr, 2);
    }else if(size > 0){
      std::cout << size << "\n";
      iovecs[i].iov_base = (char*)(std::malloc(size+12)) + 10;
      iovecs[i].iov_len = size;
      std::memcpy((char*)(iovecs[i].iov_base) - 10, finalStr, strlen(finalStr));
      std::memcpy((char*)(iovecs[i].iov_base) + size, endStr, 2);
    }else{
      iovecs[i].iov_base = std::malloc(strlen(endMsgStr));
      iovecs[i].iov_len = strlen(endMsgStr);
      std::memcpy(iovecs[i].iov_base, endMsgStr, strlen(endMsgStr));
    }
    size -= read_size;
  }
  readv(file_fd, &iovecs[num_prepend_iovecs], iovec_count - 1); //other than the last iovec

  for(int i = num_prepend_iovecs; i < iovec_count + num_prepend_iovecs - 1; i++){ //excluding the last iovec
    iovecs[i].iov_base = (char*)(iovecs[i].iov_base) - 10;
    iovecs[i].iov_len = iovecs[i].iov_len + 12;
  }

  iovec_count += num_prepend_iovecs; //increment appropriately
  
  return original_size;
}

bool read_file_web(std::string filepath, iovec *&iovecs, int &iovec_count){
  const auto header_ok = "HTTP/1.1 200 OK\r\n";
  const auto content_type = getContentType(filepath);
  const auto size = read_file_and_prep_iovecs(filepath, iovecs, iovec_count, 1); //prepending 1 iovec
  //const auto content_length = std::to_string(size);
  const auto header = header_ok + content_type + "Transfer-Encoding: chunked\r\n\r\n";
  std::cout << header << "\n";

  if(size < 0) return false;
  
  iovecs[0].iov_base = std::malloc(header.size());
  iovecs[0].iov_len = header.size();
  std::memcpy(iovecs[0].iov_base, header.c_str(), header.size());

  for(int i = 0; i < iovec_count; i++){
    std::cout << "a\n";
    for(int j = 0; j < iovecs[i].iov_len; j++){
      fputc(((char*)(iovecs[i].iov_base))[j], stdout);
    }
  }

  return true;
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

    iovec *iovecs_write = nullptr;
    int iovec_count_write = 0;
    
    if(read_file_web(processed_path, iovecs_write, iovec_count_write)){
      web_server->add_write_req(client_fd, iovecs_write, iovec_count_write); //pass the data to the write function
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