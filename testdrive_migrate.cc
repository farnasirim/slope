#include "testdrive_migrate.h"

#include <unistd.h>
#include <thread>
#include <sstream>

#include "ib.h"
#include "ib_container.h"
#include "modify_qp.h"

#include "debug.h"
#include "mig.h"
#include "allocator.h"

#include "control.h"


#include "json.hpp"

using json = nlohmann::json;

void testdrive_migrate(typename
    slope::control::RdmaControlPlane<td_mig_type>::ptr control_plane) {
  if(control_plane->self_name() == control_plane->cluster_nodes().front()) {
    debout("start testdrive_migrate");
    slope::mig_ptr<td_mig_type> ptr;
    debout("finished creating a vector of int of size 0");
    {
      debout("acquiring a context for adding elements to the vector");
      auto lock = ptr.create_context();
      for (int i = 0; i < 10; i++) {
        ptr.get()->push_back(i);
      }
    }

    deb(*ptr.get());
    for(auto it: ptr.get_pages()) {
      std::stringstream out;
      out << std::showbase << std::internal << std::setfill('0')
          << " @" << std::hex << std::setw(16) << it.first;
      out << " " << it.second;
      debout(out.str());
    }
    debout("vector contents:");
    deb(*ptr.get());

    debout("start the migration");
    deb(control_plane->cluster_nodes());
    // control_plane->simple_send();
    auto operation =
      control_plane->init_migration(control_plane->cluster_nodes()[1], ptr);
    // use ptr without alloc/dealloc
    for (unsigned int i = 1; i < 1e8; i++) {
      (*ptr.get())[0] = 10;
    }
    operation->commit();
  } else {
    // control_plane->simple_recv();
    while(true) {
      auto migrated_ptr = control_plane->poll_migrate();
      debout("null");
      if (migrated_ptr.get() != nullptr) {
        debout("returned");
        // deb((*migrated_ptr.get()));
        for(auto it: migrated_ptr.get_pages()) {
          std::stringstream out;
          out << std::showbase << std::internal << std::setfill('0')
              << " @" << std::hex << std::setw(16) << it.first;
          out << " " << it.second;
          debout(out.str());
        }
        debout("vector contents:");
        deb(*migrated_ptr.get());
        deb((*migrated_ptr.get()).size());
        migrated_ptr.collect_pages();
        break;
      }
    }
    debout("done");
  }
}
