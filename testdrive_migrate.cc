#include "testdrive_migrate.h"

#include <unistd.h>
#include <thread>
#include <sstream>

#include "ib.h"
#include "ib_container.h"
#include "logging.h"
#include "modify_qp.h"

#include "debug.h"
#include "mig.h"
#include "allocator.h"

#include "control.h"

template<typename T>
using mig_vector = std::vector<T, slope::alloc::FixedPoolAllocator<T>>;

template<typename T>
using mig_alloc = slope::alloc::FixedPoolAllocator<T>;

#include "json.hpp"

using json = nlohmann::json;

void testdrive_migrate(slope::control::RdmaControlPlane::ptr control_plane) {
  debout("start testdrive_migrate");
  slope::mig_ptr<mig_vector<int>> ptr;
  debout("finished creating a vector of int of size 0");
  {
    debout("acquiring a context for adding elements to the vector");
    auto lock = ptr.create_context();
    for(int i = 0; i <10; i++) {
      ptr.get()->push_back(i);
    }
  }

  deb(*ptr.get());


  debout("creating iterators");
  auto itt = ptr.get()->begin();
  auto bc = std::back_inserter(*ptr.get());
  debout("done creating iterators");

  std::vector<int> v(20, 123);

  deb(*ptr.get());



  for(auto it: ptr.get_pages()) {
    std::stringstream out;
    out << std::showbase << std::internal << std::setfill('0')
        << " @" << std::hex << std::setw(16) << it.first;
    out << " " << it.second;
    debout(out.str()); }

  debout("start the migration");
  deb(control_plane->cluster_nodes());
  if(control_plane->self_name() == control_plane->cluster_nodes().front()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // control_plane->simple_send();
    // control_plane->do_migrate(
    //     control_plane->cluster_nodes()[1], ptr.get_pages()
    //     );
  } else {
    // control_plane->simple_recv();
    // while(true) {
    //   std::this_thread::sleep_for(std::chrono::milliseconds(10));
    //   auto got_migration = control_plane->poll_migrate<mig_vector<int>>();
    //   if(got_migration.get() != nullptr) {
    //     break;
    //   }
    //   // if(got_migration.get() != nullptr) {
    //   //   break;
    //   // }
    // }
    // debout("done");
  }

}
