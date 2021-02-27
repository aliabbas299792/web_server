#ifndef CENTRAL_WEB_SERVER
#define CENTRAL_WEB_SERVER

#include <thread>
#include <sys/inotify.h>
#include "../server.h"

template<server_type T>
class web_server;

using tls_web_server = web_server<server_type::TLS>;
using plain_web_server = web_server<server_type::NON_TLS>;
using tls_server = server<server_type::TLS>;
using plain_server = server<server_type::NON_TLS>;

class central_web_server {
private:
  std::unordered_map<char*, int> buff_ptr_to_uses_map{};
  std::vector<std::thread> thread_container{};
  //only one of the below is used, depending on if TLS or plain web server mode is being used
  std::vector<web_server<server_type::TLS>*> thread_tls_web_servers_ptr{};
  std::vector<web_server<server_type::NON_TLS>*> thread_plain_web_servers_ptr{};

  std::unordered_map<std::string, std::string> config_data_map;

  void tls_thread_server_runner(int thread_tls_web_servers_ptr_idx);
  void plain_thread_server_runner(int thread_plain_web_servers_ptr_idx);

  central_web_server() {};
  
  std::vector<int> inotify_fds{};
  
public:
  //explicitly copy constructor/copy assignment operator
  central_web_server(central_web_server const&) = delete;
  void operator=(central_web_server const&) = delete;

  static central_web_server &get_instance(){
    static central_web_server instance{};

    return instance;
  }

  ~central_web_server(){
    for(const auto &fd : inotify_fds){
      std::cout << "closing fd " << fd << "\n";
      close(fd); //close all the inotify fds
    }

    std::cout << thread_container.size() << "\n";

    for(auto &thread : thread_container){
      std::cout << "joining...\n";
      thread.~thread();
      thread.join();
    }
  }

  int get_inotify_fd(){
    auto inotify_fd = inotify_init();
    inotify_fds.push_back(inotify_fd);
    std::cout << inotify_fd << " is inotify fd\n";
    return inotify_fd;
  }

  void start(const char *config_file_path){
    auto file_fd = open(config_file_path, O_RDONLY);
    if(file_fd == -1)
      fatal_error("Ensure the .config file is in this directory");
    auto file_size = get_file_size(file_fd);
    
    std::vector<char> config(file_size+1);
    int read_amount = 0;
    while(read_amount != file_size)
      read_amount += read(file_fd, &config[0], file_size - read_amount);
    config[read_amount] = '\0';  //sets the final byte to NULL so that strtok_r stops there

    close(file_fd);
    
    std::vector<std::vector<char>> lines;
    char *begin_ptr = &config[0];
    char *line = nullptr;
    char *saveptr = nullptr;
    while((line = strtok_r(begin_ptr, "\n", &saveptr))){
      begin_ptr = nullptr;
      lines.emplace(lines.end(), line, line + strlen(line));
    }
    
    for(auto line : lines){
      int shrink_by = 0;
      const auto length = line.size();
      for(int i = 0; i < length; i++){ //removes whitespace
        if(line[i] ==  ' ')
          shrink_by++;
        else
          line[i-shrink_by] = line[i];
      }
      if(shrink_by)
        line[length-shrink_by] = 0; //sets the byte immediately after the last content byte to NULL so that strtok_r stops there
      if(line[0] == '#') continue; //this is a comment line, so ignore it
      char *saveptr = nullptr;
      std::string key = strtok_r(&line[0], ":", &saveptr);
      std::string value = strtok_r(nullptr, ":", &saveptr);
      config_data_map[key] = value;    
    }

    if(config_data_map.count("TLS") && config_data_map["TLS"] == "yes"){
      if(!config_data_map.count("FULLCHAIN") || !config_data_map.count("PKEY") || !config_data_map.count("TLS_PORT"))
        fatal_error("Please provide FULLCHAIN, PKEY and TLS_PORT settings in the config file");
    }else if(!config_data_map.count("PORT")){
      fatal_error("Please provide the PORT setting in the config file");
    }

    //done reading config
    const auto num_threads = config_data_map.count("SERVER_THREADS") ? std::stoi(config_data_map["SERVER_THREADS"]) : 3; //by default uses 3 threads

    std::cout << "Using " << num_threads << " threads\n";

    for(int i = 0; i < num_threads; i++){
      if(config_data_map["TLS"] == "yes"){
        thread_tls_web_servers_ptr.emplace(thread_tls_web_servers_ptr.begin());
        auto &front_item = thread_tls_web_servers_ptr[thread_tls_web_servers_ptr.size()-1];
        thread_container.push_back(std::thread(&central_web_server::tls_thread_server_runner, this, i));
      }else{
        thread_plain_web_servers_ptr.emplace(thread_plain_web_servers_ptr.begin());
        thread_container.push_back(std::thread(&central_web_server::plain_thread_server_runner, this, i));
      }
    }
    
    while(true){
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
};

#endif