#include <cstdio>
#include <unistd.h>
#include <thread>
#include <assert.h>
#include <mutex>
#include <errno.h>
#include <signal.h>

#include <sys/mman.h>

#include <iostream>
#include <vector>
#include <memory>

#include "/home/colonelmo/workspace/boilerplace/cpp-debug-h/debug.h"

#include <atomic>
std::atomic_int prog;

size_t page_size;
size_t num_pages;
size_t sz;

std::mutex m;

void *mem;

void handler(int signo, siginfo_t *info, void *context) {
  std::lock_guard<std::mutex> l(m);
  std::cout << " @ " << " : " << "here: #" << std::this_thread::get_id() << " " << signo << " " << info->si_addr << " prog: " << prog << std::endl;
  std::exception ex;
  throw ex;
}

void f() {
  std::cout << "did sigmask" << std::endl;

  while(true) {
    prog ++;
  }

}

void sig_handler() {
  struct sigaction sa;

  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  // sigaddset(&sa.sa_mask, SIGSEGV);
  sa.sa_flags = SA_NODEFER | SA_RESTART | SA_SIGINFO; /* Restart functions if
                               interrupted by handler */
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    perror("sigaction");
  }
}

int main() {
  sig_handler();

  page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  num_pages = 16;
  sz = num_pages * page_size;
  mem = mmap(NULL, sz,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  int o = 0;
  deb(reinterpret_cast<void*>(mem));
  deb(reinterpret_cast<void*>(&mem));
  deb(reinterpret_cast<void*>(&o));

  if(mem == MAP_FAILED) {
    perror("mmap");
    assert(false);
  }
  printf("mmap done\n");


  int mprotect_ret = mprotect(mem, page_size * num_pages, PROT_NONE);
  if(mprotect_ret != 0) {
    perror("mprotect");
    assert(false);
  }

  std::thread t(f);

  // std::thread t2([&mem]{
  //     std::this_thread::sleep_for(std::chrono::seconds(2));
  //   printf("%d", ((int *)mem)[0]);
  //     });

  auto int_mem = static_cast<int*>(mem);

  // std::thread tt([&int_mem]{
  //   auto _ = int_mem[16];
  //     });
  try {
    printf("%d", ((int *)mem)[16]);
  } catch(...) {
    std::cout << "caught" << std::endl;
  }
  printf("before wait\n");
  // for(size_t i = 0; i < num_pages; i++) {
  //   ((char *)mem)[i * page_size] = 0;
  // }
  printf("written\n");
  // for(size_t i = 0; i < num_pages/2; i++) {
  //   munmap(&(((char *)mem)[i * page_size]), page_size);
  // }
  printf("deallocd\n");

  t.join();
  return 0;
} 
