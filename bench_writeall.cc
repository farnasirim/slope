#include "bench_writeall.h"

#include "allocator.h"
#include "logging.h"
#include "memcached_kv.h"
#include "mig.h"
#include "rdma_control.h"
#include "stat.h"

namespace slope {
namespace bench {
namespace writeall {

void run(std::string self_id, std::vector<std::string> peers,
         std::unique_ptr<slope::keyvalue::KeyValuePrefixMiddleware> kv,
         std::map<std::string, std::string> params) {
  std::sort(peers.begin(), peers.end());

  auto cp = std::make_unique<slope::control::RdmaControlPlane<MigVector32>>(
      self_id, peers, std::move(kv));

  assert(params.size() == 1);
  uint32_t num_pages = std::stoul(params[num_pages_param]);
  assert(num_pages > 0);
  slope::stat::set_param_meta(num_pages_param, params[num_pages_param]);
  uint32_t num_entries = (num_pages - 1) * slope::alloc::page_size / 4 + 1;

  if (cp->self_name() == cp->cluster_nodes().front()) {
    debout("start testdrive_migrate");
    slope::mig_ptr<MigVector32> ptr;
    debout("finished creating a vector of int of size 0");
    {
      debout("acquiring a context for adding elements to the vector");
      auto ctx = ptr.create_context();
      deb(num_entries);
      ptr.get()->resize(num_entries);
    }
    deb(ptr.get()->size());
    // std::iota(ptr.get()->begin(), ptr.get()->end(), 0);

    // deb(*ptr.get());
    for (auto it : ptr.get_chunks()) {
      std::stringstream out;
      out << std::showbase << std::internal << std::setfill('0') << " @"
          << std::hex << std::setw(16) << it.first;
      out << " " << it.second;
      debout(out.str());
    }
    debout("vector contents:");
    // deb(*ptr.get());

    debout("start the migration");
    deb(cp->cluster_nodes());
    slope::stat::add_value(slope::stat::key::operation, "call init_migration");
    auto operation = cp->init_migration(cp->cluster_nodes()[1], ptr);
    slope::stat::add_value(slope::stat::key::operation,
                           "start calling try_commit");
    while (operation->get_ready_state() != 1) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    for (size_t i = 0; i < ptr.get()->size(); i += 1024) {
      (*ptr.get())[i] = 1;
    }
    // deb(*ptr.get());
    while (true) {
      if (operation->try_commit()) {
        break;
      }
    }
    slope::stat::add_value(slope::stat::key::operation, "finished try_commit");
    operation->collect();
    slope::stat::add_value(slope::stat::key::operation, "finished collect");
  } else {
    // cp->simple_recv();
    while (true) {
      auto migrated_ptr = cp->poll_migrate();
      // debout("null");
      if (migrated_ptr.get() != nullptr) {
        debout("returned");
        // deb((*migrated_ptr.get()));
        slope::stat::add_value(slope::stat::key::operation, "got ptr");
#ifdef SLOPE_DEBUG
        for (auto it : migrated_ptr.get_chunks()) {
          std::stringstream out;
          out << std::showbase << std::internal << std::setfill('0') << " @"
              << std::hex << std::setw(16) << it.first;
          out << " " << it.second;
          debout(out.str());
        }
#endif
        slope::stat::add_value(slope::stat::key::operation,
                               "called collect_pages");
        migrated_ptr.collect_pages();
        // deb((*migrated_ptr.get()));
        slope::stat::add_value(slope::stat::key::operation,
                               "finished collect_pages");
        break;
      }
    }
    debout("done");
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

}  // namespace writeall
}  // namespace bench
}  // namespace slope


