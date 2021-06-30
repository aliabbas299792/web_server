#ifndef DATA_STORE
#define DATA_STORE

#include <vector>
#include <unordered_set>

#include <iostream>

// If using this across multiple threads, only call free_item/allocate_item on one thread,
// and only those, after you're sure that (when deleting) the ptr you're using is definitely
// done with now (i.e use eventfd and some struct to communicate on whether or not a thread is done)

class data_store_raw {
  std::vector<std::pair<void*, int>> data_vec{};
  std::unordered_set<int> free_idxs{};

public:
  void free_item(int idx){ // frees the item if it has 0 uses left
    auto &item = data_vec[idx];
    if(--item.second == 0){
      free(data_vec[idx].first);
      data_vec[idx] = { nullptr, -1 };
      free_idxs.insert(idx);
    }
  }

  std::pair<void*, int> allocate_item(size_t size, int uses){ // used to allocate and insert an item
    int idx = 0;
    if(free_idxs.size() > 0){
      idx = *free_idxs.cbegin();
      free_idxs.erase(idx);
    }else{
      data_vec.emplace_back();
      idx = data_vec.size() - 1;
    }

    auto ptr = std::malloc(size);
    data_vec[idx] = { ptr, uses };
    return { ptr, idx };
  }
  
  int insert_item(void *ptr, int uses){ // inserts and from then on assume the ptr belongs to this data store
    int idx = 0;
    if(free_idxs.size() > 0){
      idx = *free_idxs.cbegin();
      free_idxs.erase(idx);
    }else{
      data_vec.emplace_back();
      idx = data_vec.size() - 1;
    }

    data_vec[idx] = { ptr, uses };
    return idx;
  }

  void *get_item(int idx){
    return data_vec[idx].first;
  }
};

class data_store {
  std::vector<std::pair<std::vector<char>, int>> data_vec{};
  std::unordered_set<int> free_idxs{};

  struct buff {
    void *ptr{};
    size_t size{};
    buff(void *ptr = nullptr, size_t size = -1) : ptr(ptr), size(size) {}
  };

  struct buff_idx_pair {
    buff buffer{};
    int idx{};
    buff_idx_pair(buff buffer, int idx) : buffer(buffer), idx(idx) {}
  };

public:
  void free_item(int idx){ // frees the item if it has 0 uses left
    auto &item = data_vec[idx];
    if(--item.second == 0){
      data_vec[idx] = { {}, -1 };
      printf("finally i am free\n");
      free_idxs.insert(idx);
    }
  }

  buff_idx_pair make_item(size_t size, int uses){ // used to allocate and insert an item
    int idx = 0;
    if(free_idxs.size() > 0){
      idx = *free_idxs.cbegin();
      free_idxs.erase(idx);
    }else{
      data_vec.emplace_back();
      idx = data_vec.size() - 1;
    }

    data_vec[idx].first.resize(size);
    return { { data_vec[idx].first.data(), size }, idx };
  }
  
  buff_idx_pair insert_item(std::vector<char> &&buff, int uses){ // inserts and from then on assume the buff belongs to this data store
    int idx = 0;
    if(free_idxs.size() > 0){
      idx = *free_idxs.cbegin();
      free_idxs.erase(idx);
    }else{
      data_vec.emplace_back();
      idx = data_vec.size() - 1;
    }

    data_vec[idx] = { std::move(buff), uses };
    return { { data_vec[idx].first.data() , data_vec[idx].first.size() }, idx };
  }

  void *get_item(int idx){
    return data_vec[idx].first.data();
  }
};

#endif