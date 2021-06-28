#ifndef DATA_STORE
#define DATA_STORE

#include <vector>
#include <unordered_set>

// If using this across multiple threads, only call free_item/allocate_item on one thread,
// and only those, after you're sure that (when deleting) the ptr you're using is definitely
// done with now (i.e use eventfd and some struct to communicate on whether or not a thread is done)

class data_store {
  std::vector<void*> data_vec{};
  std::unordered_set<int> free_idxs{};

public:
  void free_item(int idx){ // frees the item
    free(data_vec[idx]);
    data_vec[idx] = nullptr;
    free_idxs.insert(idx);
  }

  std::pair<void*, int> allocate_item(size_t size){ // used to allocate and insert an item
    int idx = 0;
    if(free_idxs.size() > 0){
      idx = *free_idxs.cbegin();
      free_idxs.erase(idx);
    }else{
      data_vec.emplace_back();
      idx = data_vec.size() - 1;
    }

    auto ptr = std::malloc(size);
    data_vec[idx] = ptr;
    return { ptr, idx };
  }
  
  int insert_item(void *ptr){ // inserts and from then on assume the ptr belongs to this data store
    int idx = 0;
    if(free_idxs.size() > 0){
      idx = *free_idxs.cbegin();
      free_idxs.erase(idx);
    }else{
      data_vec.emplace_back();
      idx = data_vec.size() - 1;
    }

    data_vec[idx] = ptr;
    return idx;
  }

  void *get_item(int idx){
    return data_vec[idx];
  }
};

#endif