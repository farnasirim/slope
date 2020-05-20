#include <cstdio>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>

#include <sys/mman.h>

#include <iostream>
#include <vector>
#include <memory>

#include "debug.h"
#include "mig.h"
#include "allocator.h"

template<typename T>
using mig_vector = std::vector<T, slope::alloc::FixedPoolAllocator<T>>;

template<typename T>
using mig_alloc = slope::alloc::FixedPoolAllocator<T>;

std::shared_ptr<mig_vector<int>> f(std::shared_ptr<mig_vector<int>> vec);
std::shared_ptr<mig_vector<int>> f(std::shared_ptr<mig_vector<int>> vec) {
  debout("in f");
  return vec;
}

struct Base {
  int data[10];
  Base() { std::cout << "  Base::Base()\n"; }
  // Note: non-virtual destructor is OK here
  ~Base() { std::cout << "  Base::~Base()\n"; }
};

std::ostream& operator<<(std::ostream& os, const Base& b);
std::ostream& operator<<(std::ostream& os, const Base& b) {
  return os << std::vector<int>(b.data, b.data + 10);
}

int main() {
  // auto vec = std::allocate_shared<mig_vector<int>, slope::alloc::FixedPoolAllocator<mig_vector<int>>>(
  //     slope::alloc::FixedPoolAllocator<int>(), 123);

  slope::mig_ptr<mig_vector<int>> ptr(static_cast<size_t>(10), 2);
  debout("created");
  ptr.get()->push_back(1);

  for(auto it: *ptr.get()) {
    std::cout << it <<  " ";
  }
  std::cout << std::endl;

  // vec = f(vec);

  // debout("out f");

  // auto vec = std::allocate_shared<mig_vector<Base>, mig_alloc<Base>>(mig_alloc<Base>());
  // debout("made vec");

  // Base b;
  // for(int i = 0; i <10; i++) {
  //   b.data[i] = i * i;
  // }
  // Base b2;
  // for(int i = 0; i <10; i++) {
  //   b2.data[i] = i + (i ? b2.data[i - 1] : 0);
  // }
  // debout("made bases");

  // vec->push_back(b);
  // debout("pushed b");
  // vec->push_back(b2);
  // debout("pushed b2");


  // deb(*vec);
  // mig_vector<Base> *actual = vec.get();

  // deb(*actual);



  // size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  // deb(sz/1e9);
  // printf("%zu\n", sz);
  // int *p = new int;
  // void *mem = mmap(reinterpret_cast<void*>(p), sz,
  //     PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  // int o = 0;
  // deb(reinterpret_cast<void*>(mem));
  // deb(reinterpret_cast<void*>(&mem));
  // deb(reinterpret_cast<void*>(p));
  // deb(reinterpret_cast<void*>(&o));

  // if(mem == MAP_FAILED) {
  //   perror("mmap");
  //   assert(false);
  // }
  // printf("mmap done\n");

  // struct sigaction sa;

  // sa.sa_sigaction = handler;
  // sigemptyset(&sa.sa_mask);
  // sa.sa_flags = SA_RESTART; /* Restart functions if
  //                              interrupted by handler */
  // if (sigaction(SIGSEGV, &sa, NULL) == -1) {
  //   perror("sigaction");
  // }


  // // int mprotect_ret = mprotect(mem, page_size * num_pages, PROT_NONE);
  // // if(mprotect_ret != 0) {
  // //   perror("mprotect");
  // //   assert(false);
  // // }

  // // printf("%d", ((int *)mem)[45]);
  // printf("before wait\n");
  // // for(size_t i = 0; i < num_pages; i++) {
  // //   ((char *)mem)[i * page_size] = 0;
  // // }
  // printf("written\n");
  // for(size_t i = 0; i < num_pages/2; i++) {
  //   munmap(&(((char *)mem)[i * page_size]), page_size);
  // }
  // printf("deallocd\n");

  return 0;
}
