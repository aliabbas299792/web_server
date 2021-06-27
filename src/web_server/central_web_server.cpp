#include "../header/web_server/web_server.h"
#include <thread>

std::unordered_map<std::string, std::string> central_web_server::config_data_map{};

template<>
void central_web_server::thread_server_runner(tls_web_server &basic_web_server){
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

  tcp_server.start();
}

template<>
void central_web_server::thread_server_runner(plain_web_server &basic_web_server){
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

void central_web_server::start_server(const char *config_file_path){
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

  if(config_data_map["TLS"] == "yes")
    run<server_type::TLS>(num_threads);
  else
    run<server_type::NON_TLS>(num_threads);
}

void central_web_server::add_event_read_req(int event_fd){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  auto *req = new central_web_server_req(); //enough space for the request struct
  req->buff.resize(sizeof(uint64_t));
  req->event = central_web_server_event::EVENTFD;
  req->fd = event_fd;
  
  io_uring_prep_read(sqe, event_fd, &(req->buff[0]), sizeof(uint64_t), 0); //don't read at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event
}

void central_web_server::add_read_req(int fd, size_t size){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  auto *req = new central_web_server_req(); //enough space for the request struct
  req->buff.resize(size);
  req->event = central_web_server_event::READ;
  req->fd = fd;
  
  io_uring_prep_read(sqe, fd, &(req->buff[0]), size, 0); //don't read at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event
}

void central_web_server::add_write_req(int fd, const char *buff_ptr, size_t size){
  auto *req = new central_web_server_req();
  req->buff_ptr = buff_ptr;
  req->size = size;
  req->fd = fd;

  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, fd, buff_ptr, size, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event
}

void central_web_server::read_req_continued(central_web_server_req *req, size_t last_read){
  req->progress_bytes += last_read;
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  //the fd is stored in the custom info bit
  io_uring_prep_read(sqe, (int)req->fd, &(req->buff[req->progress_bytes]), req->buff.size() - req->progress_bytes, req->progress_bytes);
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event
}

void central_web_server::write_req_continued(central_web_server_req *req, size_t written){
  req->progress_bytes += written;
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  // again, buff_ptr is used for writing, progress_bytes is how much has been written/read (written in this case)
  io_uring_prep_write(sqe, req->fd, &req->buff_ptr[req->progress_bytes], req->size - req->progress_bytes, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event
}

template<server_type T>
void central_web_server::run(int num_threads){
  std::cout << "Using " << num_threads << " threads\n";

  const auto make_ws_frame = config_data_map["TLS"] == "yes" ? web_server<server_type::TLS>::make_ws_frame : web_server<server_type::NON_TLS>::make_ws_frame;
  auto str = "Hello world";
  auto ws_data = make_ws_frame(str, websocket_non_control_opcodes::text_frame);

  std::vector<server_data<T>> thread_data_container{};
  thread_data_container.resize(num_threads);

  audio_broadcaster broadcaster(event_fd);
  std::thread audio_worker_thread(&audio_broadcaster::audio_thread, &broadcaster);

  // the main io_uring loop

  std::memset(&ring, 0, sizeof(io_uring));
  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue

  io_uring_cqe *cqe;

  add_event_read_req(event_fd); // need to read on this
  
  bool run_server = true;

  while(run_server){
    char ret = io_uring_wait_cqe(&ring, &cqe);

    if(ret < 0){
      io_uring_queue_exit(&ring);
      close(event_fd);
      break;
    }

    auto *req = reinterpret_cast<central_web_server_req*>(cqe->user_data);

    if(cqe->res < 0){
      std::cerr << "CQE RES CENTRAL: " << cqe->res << std::endl;
      std::cerr << "ERRNO: " << errno << std::endl;
      std::cerr << "io_uring_wait_cqe ret: " << ret << std::endl;
      io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
      continue;
    }

    switch (req->event) {
      case central_web_server_event::EVENTFD: {
        const auto signal = *reinterpret_cast<uint64_t*>(req->buff.data());
        switch (signal) {
          case central_web_server_signals::KILL_SERVER:
            io_uring_queue_exit(&ring);
            close(event_fd);
            run_server = false;
            break;
          case central_web_server_signals::WORKER_THREAD_QUEUE:
            // broadcast this to the server threads
            break;
        }
        add_event_read_req(event_fd); // rearm the read request
        break;
      }
      case central_web_server_event::READ:
        if(req->buff.size() == cqe->res + req->progress_bytes){
          // the entire thing has been read, add it to some local cache or something
        }else{
          read_req_continued(req, cqe->res);
          req = nullptr;
        }
        break;
      case central_web_server_event::WRITE:
        if(cqe->res + req->progress_bytes < req->size){ // if there is still more to write, then write
          write_req_continued(req, cqe->res);
        }else{
          // we're finished writing otherwise
        }
        break;
    }

    delete req;
    
    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }

  // wait for all threads to exit before exiting the program
  for(auto &thread_data : thread_data_container)
    thread_data.thread.join();

  audio_worker_thread.join();
}

void central_web_server::kill_server(){
  auto kill_sig = central_web_server_signals::KILL_SERVER;
  write(event_fd, &kill_sig, sizeof(kill_sig));

  server<server_type::TLS>::kill_all_servers(); // kills all TLS servers
  server<server_type::NON_TLS>::kill_all_servers(); // kills all non TLS servers
  // this will mean the run() function will exit
}

void audio_broadcaster::audio_thread(){
  // something
}