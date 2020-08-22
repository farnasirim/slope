#include "bench_readonly.h"

#include "allocator.h"
#include "logging.h"
#include "memcached_kv.h"
#include "mig.h"
#include "rdma_control.h"
#include "stat.h"

namespace slope {
namespace bench {
namespace readonly {

void run(std::string self_id, std::vector<std::string> peers,
         std::unique_ptr<slope::keyvalue::KeyValuePrefixMiddleware> kv,
         std::map<std::string, std::string> params) {
  std::sort(peers.begin(), peers.end());

  auto cp = std::make_unique<slope::control::RdmaControlPlane<MigVector32>>(
      self_id, peers, std::move(kv));

  if (cp->self_name() == cp->cluster_nodes().front()) {
    debout("start testdrive_migrate");
    slope::mig_ptr<MigVector32> ptr;
    debout("finished creating a vector of int of size 0");
    {
      debout("acquiring a context for adding elements to the vector");
      auto lock = ptr.create_context();
      for (uint32_t i = 0; i < 100; i++) {
        ptr.get()->push_back(i);
      }
    }

    deb(*ptr.get());
    for (auto it : ptr.get_pages()) {
      std::stringstream out;
      out << std::showbase << std::internal << std::setfill('0') << " @"
          << std::hex << std::setw(16) << it.first;
      out << " " << it.second;
      debout(out.str());
    }
    debout("vector contents:");
    deb(*ptr.get());

    debout("start the migration");
    deb(cp->cluster_nodes());
    // cp->simple_send();
    auto operation = cp->init_migration(cp->cluster_nodes()[1], ptr);
    // use ptr without alloc/dealloc
    while (true) {
      if (operation->try_commit()) {
        break;
      }
    }
    operation->collect();
  } else {
    // cp->simple_recv();
    while (true) {
      auto migrated_ptr = cp->poll_migrate();
      debout("null");
      if (migrated_ptr.get() != nullptr) {
        debout("returned");
        // deb((*migrated_ptr.get()));
        for (auto it : migrated_ptr.get_pages()) {
          std::stringstream out;
          out << std::showbase << std::internal << std::setfill('0') << " @"
              << std::hex << std::setw(16) << it.first;
          out << " " << it.second;
          debout(out.str());
        }
        migrated_ptr.collect_pages();
        break;
      }
    }
    debout("done");
  }

  // assert(params.size() == 1);

  // uint32_t num_pages = std::stoul(params[num_pages_param]);
  // assert(num_pages > 0);
  // slope::stat::set_param_meta(num_pages_param, params[num_pages_param]);

  // uint32_t num_entries = (num_pages - 1) * slope::alloc::page_size / 4 + 1;

  // if (self_id == peers[0]) {
  //   slope::mig_ptr<MigVector32> ptr;
  //   {
  //     auto lock = ptr.create_context();
  //     for (uint32_t i = 0; i < num_entries; i++) {
  //       ptr.get()->push_back(i % 256);
  //     }
  //   }

  //   deb(*ptr.get());
  //   for (auto it : ptr.get_pages()) {
  //     std::stringstream out;
  //     out << std::showbase << std::internal << std::setfill('0') << " @"
  //         << std::hex << std::setw(16) << it.first;
  //     out << " " << it.second;
  //     debout(out.str());
  //   }

  //   debout("vector contents:");
  //   deb(*ptr.get());

  //   debout("start the migration");
  //   slope::stat::add_value(slope::stat::key::operation, "init_migration");
  //   auto operation = cp->init_migration(peers[1], ptr);
  //   slope::stat::add_value(slope::stat::key::operation, "collect");
  //   operation->collect();
  //   slope::stat::add_value(slope::stat::key::operation, "finished collect");
  // } else {
  //   debout("waiting");
  //   while (true) {
  //     auto migrated_ptr = cp->poll_migrate();
  //     if (migrated_ptr.get() != nullptr) {
  //       debout("inside");
  //       slope::stat::add_value(slope::stat::key::operation, "got ptr");
  //       for (auto it : migrated_ptr.get_pages()) {
  //         std::stringstream out;
  //         out << std::showbase << std::internal << std::setfill('0') << " @"
  //             << std::hex << std::setw(16) << it.first;
  //         out << " " << it.second;
  //         debout(out.str());
  //       }
  //       debout("vector contents:");
  //       deb(*migrated_ptr.get());
  //       deb((*migrated_ptr.get()).size());
  //       migrated_ptr.collect_pages();
  //       migrated_ptr.collect_pages();
  //       slope::stat::add_value(slope::stat::key::operation, "collected
  //       pages"); break;
  //     }
  //   }
  //   debout("done");
  // }
}

}  // namespace readonly
}  // namespace bench
}  // namespace slope


