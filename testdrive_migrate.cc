#include "testdrive_migrate.h"

#include "discovery.h"
#include "ib.h"
#include "ib_container.h"
#include "logging.h"
#include "modify_qp.h"

#include "debug.h"
#include "mig.h"
#include "allocator.h"

template<typename T>
using mig_vector = std::vector<T, slope::alloc::FixedPoolAllocator<T>>;

template<typename T>
using mig_alloc = slope::alloc::FixedPoolAllocator<T>;

void do_src() {
  debout("first");
  slope::mig_ptr<mig_vector<int>> ptr(static_cast<size_t>(10), 0);
  std::cout << "before" << std::endl;
  {
    auto lock = ptr.create_context();
    for(size_t i = 0;i < 10; i++) {
      (*ptr.get())[i] = i;
    }
  }

  for(size_t i = 0;i < 10; i++) {
    std::cout << (*ptr.get())[i] << " ";
  }
  std::cout << std::endl;

  std::cout << "done" << std::endl;
  deb(*ptr.get());

  for(auto it: ptr.get_pages()) {
    std::cout << std::hex << it << std::endl;
    // rdma address: it.first with size it.second to another machine
  }



  IbvDeviceContextByName ib_context("mlx5_1");
  IbvAllocPd pd(ib_context.get());

  int flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

}

void do_snk() {

}

void testdrive_migrate(int machine_id) {
  if(machine_id == 0) {
    do_src();
  } else {
    do_snk();
  }
}
