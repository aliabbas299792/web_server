#ifndef CACHE
#define CACHE

#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "common_structs_enums.h"

struct cache_item {
  std::vector<char> buffer{};
  timespec timestamp{};
  int lock_number{}; //number of times this has been locked, if non zero then this item should NOT be removed (in use)
  int next_item_idx = -1;
  int prev_item_idx = -1;
  int fd{};
};

struct cache_fetch_item {
  cache_fetch_item(bool found, char *buff_ptr, size_t size = -1) : found(found), buff(buff_ptr), size(size) {}
  bool found = false;
  size_t size{};
  char *buff{};
};

template<int N>
class cache{
private:
  std::unordered_map<std::string, int> filepath_to_cache_idx{};
  std::unordered_map<int, std::string> cache_idx_to_filepath{};
  std::array<cache_item, N> cache_buffer{};
  std::unordered_set<int> free_idxs{}; //only used at the beginning of the program, and when files are modified
  std::unordered_map<int, int> client_idx_to_cache_idx{}; //used in setting/unsetting locks
  int highest_idx = -1;
  int lowest_idx = -1;
public:
  cache(){ //only works for cache's which are greater than 1 in size
    for(int i = 0; i < cache_buffer.size(); i++)
      free_idxs.insert(i);
  }

  cache_fetch_item fetch_item(const std::string &filepath, int client_idx, tcp_client &client){
    if(filepath_to_cache_idx.count(filepath)) {
      auto current_idx = filepath_to_cache_idx[filepath];
      auto &item = cache_buffer[current_idx];

      client.using_file = true; //we are using a file, we have incremented the lock number once
      cache_buffer[current_idx].lock_number++;

      client_idx_to_cache_idx[client_idx] = current_idx; //mapping to the idx that the client_idx is locking

      const auto new_timestamp = get_timestamp(item.fd);
      bool outdated_file = item.timestamp.tv_sec < new_timestamp.tv_sec || (item.timestamp.tv_sec == new_timestamp.tv_sec && item.timestamp.tv_nsec < new_timestamp.tv_nsec);

      if(item.next_item_idx == -1){ //cannot promote highest one more
        if(!outdated_file){
          return { true, &(cache_buffer[current_idx].buffer[0]), cache_buffer[current_idx].buffer.size() };
        }else{ //file is outdated
          if(item.prev_item_idx != -1)
            cache_buffer[item.prev_item_idx].next_item_idx = -1;
          
          close(item.fd); //remember to close files
          free_idxs.insert(current_idx);
          item = cache_item();

          return { false, nullptr };
        }
      }
      
      //otherwise promote current item to top

      if(item.prev_item_idx != -1){ //first though we've gotta remove it from its current position
        auto &prev_item = cache_buffer[item.prev_item_idx];
        auto &next_item = cache_buffer[item.next_item_idx];
        prev_item.next_item_idx = item.next_item_idx;
        next_item.prev_item_idx = item.prev_item_idx;
      }else{
        auto &next_item = cache_buffer[item.next_item_idx];
        next_item.prev_item_idx = -1;
        lowest_idx = item.next_item_idx;
      }

      if(outdated_file){
        close(item.fd); //remember to close files

        free_idxs.insert(current_idx);
        item = cache_item();

        return { false, nullptr };
      }

      cache_buffer[highest_idx].next_item_idx = current_idx;
      cache_buffer[current_idx].next_item_idx = -1;
      cache_buffer[current_idx].prev_item_idx = highest_idx;

      highest_idx = current_idx; //current item is promoted to the top

      return { true, &(cache_buffer[current_idx].buffer[0]), cache_buffer[current_idx].buffer.size() };
    }else{
      return { false, nullptr };
    }
  }
  
  bool try_insert_item(int client_idx, const std::string &filepath, std::vector<char> &&buff, int fd){
    int current_idx = -1;

    if(free_idxs.size()){ //if free idxs available
      current_idx = *free_idxs.cbegin();
      free_idxs.erase(current_idx);

      if(lowest_idx == -1)
        lowest_idx = current_idx;
    }else{
      auto &lowest_item = cache_buffer[lowest_idx];

      if(!lowest_item.lock_number){ //only if the lock_number is 0, then this item can be inserted in place of the old one (otherwise the old one is still in use)
        const auto new_lowest_idx = lowest_item.next_item_idx;

        close(lowest_item.fd); //remember to close files

        cache_buffer[lowest_item.next_item_idx].prev_item_idx = -1; //2nd lowest is now lowest
        cache_buffer[lowest_idx] = cache_item(); //we are reusing the lowest item

        filepath_to_cache_idx.erase(cache_idx_to_filepath[lowest_idx]);
        cache_idx_to_filepath.erase(lowest_idx);
        
        current_idx = lowest_idx;
        lowest_idx = new_lowest_idx;
      }
    }

    if(current_idx != -1){
      auto &current_item = cache_buffer[current_idx];
      
      if(highest_idx != -1)
        cache_buffer[highest_idx].next_item_idx = current_idx;
      
      filepath_to_cache_idx[filepath] = current_idx;
      cache_idx_to_filepath[current_idx] = filepath;

      current_item.fd = fd;
      current_item.timestamp = get_timestamp(fd);
      current_item.prev_item_idx = highest_idx; //promote to highest
      current_item.next_item_idx = -1;
      highest_idx = current_idx; //new highest position
      current_item.buffer = std::move(buff); //populate the buffer
      return true;
    }else{
      return false;
    }
  }

  void finished_with_item(int client_idx, tcp_client &client){ //requires a pointer to the client object, for the using_file stuff - to ensure it's not decremented too many times
    if(client.using_file){
      const auto cache_idx = client_idx_to_cache_idx[client_idx];
      cache_buffer[cache_idx].lock_number--;
      client.using_file = false;
    }
  }
};

#endif