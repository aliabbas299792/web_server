
struct cache_item {
  std::vector<char> buffer{};
  int next_item_idx = -1;
  int prev_item_idx = -1;
};

std::unordered_map<std::string, int> filepath_to_cache_idx{};
std::array<cache_item, 10> cache{};
std::unordered_set<int> free_idxs{}; //only used at the beginning of the program
int highest_idx = -1;
int lowest_idx = -1;

std::vector<char> &fetch(std::string filepath){
  if(filepath_to_cache_idx.count(filepath)){ //if this filepath has been cached
    auto current_idx = filepath_to_cache_idx[filepath];
    auto &item = cache[current_idx];

    if(item.next_item_idx == -1) //cannot promote highest one more
      return &item.buffer;
    
    //otherwise promote current item to top

    if(item.prev_item_idx != -1){ //first though we've gotta remove it from its current position
      auto &prev_item = cache[item.prev_item_idx];
      auto &next_item = cache[item.next_item_idx];
      prev_item.next_item_idx = item.next_item_idx;
      next_item.prev_item_idx = item.prev_item_idx;
    }else{
      auto &next_item = cache[item.next_item_idx];
      prev_item.next_item_idx = item.next_item_idx;
    }

    cache[highest_idx].next_item_idx = current_idx;
    cache[current_idx].next_item_idx = -1;
    cache[current_idx].prev_item_idx = highest_idx;

    highest_idx = current_idx;
  }else{ //file path not cached
    int current_idx = -1;

    if(free_idxs.size()){ //if free idxs available
      auto current_idx = *free_idxs.cbegin();
      free_idxs.erase(current_idx);

      if(highest_idx != -1)
        cache[highest_idx].next_item_idx = current_idx;
      if(lowest_idx == -1)
        lowest_idx = current_idx;
    }else{
      auto &lowest_item = cache[lowest_idx];
      cache[lowest_item.next_item_idx].prev_item_idx = -1; //2nd lowest is now lowest
      lowest_idx = lowest_item.next_item_idx;
      cache[lowest_idx] = cache_item(); //we are reusing the lowest item

      current_idx = lowest_idx;
    }

    auto &current_item = cache[current_idx];
    current_item.prev_item_idx = highest_idx; //promote to highest
    highest_idx = current_idx; //new highest position
    current_item.buffer = read_file(filepath); //populate the buffer
  }
}