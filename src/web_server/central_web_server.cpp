#include "../header/web_server/web_server.h"

std::unordered_map<std::string, std::string> central_web_server::config_data_map{};

void central_web_server::tls_thread_server_runner(){
  tls_web_server basic_web_server;
  tls_server tcp_server(
    std::stoi(config_data_map["TLS_PORT"]),
    config_data_map["FULLCHAIN"],
    config_data_map["PKEY"],
    &basic_web_server,
    accept_cb<server_type::TLS>,
    close_cb<server_type::TLS>,
    read_cb<server_type::TLS>,
    write_cb<server_type::TLS>,
    event_cb<server_type::TLS>,
    custom_read_cb<server_type::TLS>
  ); //pass function pointers and a custom object

  basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server
  
  std::thread server_thread([&tcp_server](){
    tcp_server.start();
  });
    
  while(true){
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

}

void central_web_server::plain_thread_server_runner(){
  plain_web_server basic_web_server;

  plain_server tcp_server(
    std::stoi(config_data_map["PORT"]),
    &basic_web_server,
    accept_cb<server_type::NON_TLS>,
    close_cb<server_type::NON_TLS>,
    read_cb<server_type::NON_TLS>,
    write_cb<server_type::NON_TLS>,
    event_cb<server_type::NON_TLS>,
    custom_read_cb<server_type::NON_TLS>
  ); //pass function pointers and a custom object
  
  basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server
  
  tcp_server.start();
}

central_web_server::central_web_server(const char *config_file_path){
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

  // the below is more like demo code to test out the multithreaded features

  //done reading config
  const auto num_threads = config_data_map.count("SERVER_THREADS") ? std::stoi(config_data_map["SERVER_THREADS"]) : 3; //by default uses 3 threads

  std::cout << "Using " << num_threads << " threads\n";

  for(int i = 0; i < num_threads; i++){
    if(config_data_map["TLS"] == "yes")
      thread_container.push_back(std::thread(tls_thread_server_runner));
    else
      thread_container.push_back(std::thread(plain_thread_server_runner));
  }
  
  while(true){
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}