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
    std::iota(ptr.get()->begin(), ptr.get()->end(), 0);

    deb(num_entries);
    deb(ptr.get()->size());
    deb(*ptr.get());
    for (auto it : ptr.get_chunks()) {
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
    slope::stat::add_value(slope::stat::key::operation, "call: init_migration");
    auto operation = cp->init_migration(cp->cluster_nodes()[1], ptr);

    slope::stat::add_value(slope::stat::key::operation,
                           "start: calling try_finish_write");
    while (true) {
      if (operation->try_finish_write()) {
        break;
      }
    }
    slope::stat::add_value(slope::stat::key::operation,
                           "finish: calling try_finish_write");

    slope::stat::add_value(slope::stat::key::operation,
                           "start: calling try_finish_read");
    while (true) {
      if (operation->try_finish_read()) {
        break;
      }
    }
    slope::stat::add_value(slope::stat::key::operation,
                           "finish: calling try_finish_read");
    operation->collect();
    slope::stat::add_value(slope::stat::key::operation, "finish: collect");
  } else {
    // cp->simple_recv();
    while (true) {
      auto migrated_ptr = cp->poll_migrate();
      debout("null");
      if (migrated_ptr.get() != nullptr) {
        debout("returned");
        deb((*migrated_ptr.get()));
        slope::stat::add_value(slope::stat::key::operation,
                               "recieved: object ptr");
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
                               "call: collect_pages");
        migrated_ptr.collect_pages();
        slope::stat::add_value(slope::stat::key::operation,
                               "finish: collect_pages");
        break;
      }
    }
    debout("done");
  }
}

}  // namespace readonly
}  // namespace bench
}  // namespace slope


