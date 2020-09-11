#include "bench_bloom.h"

#include <random>

#include "allocator.h"
#include "bloomfilter.h"
#include "logging.h"
#include "memcached_kv.h"
#include "mig.h"
#include "rdma_control.h"
#include "stat.h"

namespace slope {
namespace bench {
namespace bloomfilter {

static const int gran = 100;

void run(std::string self_id, std::vector<std::string> peers,
         std::unique_ptr<slope::keyvalue::KeyValuePrefixMiddleware> kv,
         std::map<std::string, std::string> params) {
  std::sort(peers.begin(), peers.end());
  deb(params[bloomfilter_size_param]);
  deb(params);

  auto cp = std::make_unique<slope::control::RdmaControlPlane<BloomFilter>>(
      self_id, peers, std::move(kv));

  assert(params.size() == 1);
  uint32_t bloomfilter_size = std::stoul(params[bloomfilter_size_param]);
  assert(bloomfilter_size > 0);
  slope::stat::set_param_meta(bloomfilter_size_param,
                              params[bloomfilter_size_param]);
  std::vector<std::mutex> locks(bloomfilter_size);

  if (cp->self_name() == cp->cluster_nodes().front()) {
    debout("start testdrive_migrate");
    slope::mig_ptr<BloomFilter> ptr(bloomfilter_size);
    ptr.get()->set_lock_vector(&locks);

    std::vector<std::mutex> local_locks(bloomfilter_size);
    slope::mig_ptr<BloomFilter> local(bloomfilter_size);
    local.get()->set_lock_vector(&local_locks);

    deb(ptr.get()->size());
    for (auto it : ptr.get_chunks()) {
      std::stringstream out;
      out << std::showbase << std::internal << std::setfill('0') << " @"
          << std::hex << std::setw(16) << it.first;
      out << " " << it.second;
      debout(out.str());
    }

    std::atomic_int rw_state = 0;
    std::atomic_int quit = 0;

    std::thread reader([&] {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> cnt(1, 10);
      std::uniform_int_distribution<char> chr('a', 'z');
      std::uniform_int_distribution<int> which(0, 1);
      int both = 1;
      while (!quit) {
        if (both && rw_state == 3) {
          rw_state++;
          both = 0;
        }
        int loc = 0, rem = 0;
        for (int i = 0; i < gran; i++) {
          auto sz = cnt(mt);
          std::string str;
          for (int j = 0; j < sz; j++) {
            str += chr(mt);
          }
          if (both && which(mt)) {
            ptr.get()->get(str);
            rem++;
          } else {
            local.get()->get(str);
            loc++;
          }
        }
        slope::stat::add_value(slope::stat::key::operation,
                               "src_local_read:" + std::to_string(loc));
        slope::stat::add_value(slope::stat::key::operation,
                               "src_rem_read:" + std::to_string(rem));
      }
    });

    std::thread writer([&] {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> cnt(1, 10);
      std::uniform_int_distribution<char> chr('a', 'z');
      std::uniform_int_distribution<int> which(0, 1);
      int both = 1;
      while (!quit) {
        if (both && rw_state == 1) {
          rw_state++;
          both = 0;
        }
        int loc = 0, rem = 0;
        for (int i = 0; i < gran; i++) {
          auto sz = cnt(mt);
          std::string str;
          for (int j = 0; j < sz; j++) {
            str += chr(mt);
          }
          if (both && which(mt)) {
            ptr.get()->set(str);
            rem++;
          } else {
            local.get()->set(str);
            loc++;
          }
        }
        slope::stat::add_value(slope::stat::key::operation,
                               "src_local_write:" + std::to_string(loc));
        slope::stat::add_value(slope::stat::key::operation,
                               "src_rem_write:" + std::to_string(rem));
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    debout("start the migration");
    slope::stat::add_value(slope::stat::key::operation, "call:init_migration");
    auto operation = cp->init_migration(cp->cluster_nodes()[1], ptr);

    slope::stat::add_value(slope::stat::key::operation,
                           "start: waiting for ready_state 1");
    while (operation->get_ready_state() != 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    slope::stat::add_value(slope::stat::key::operation,
                           "finish: waiting for ready_state 1");

    rw_state++;
    while (rw_state != 2) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

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
                           "start: waiting for ready_state 3");
    while (operation->get_ready_state() != 3) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    slope::stat::add_value(slope::stat::key::operation,
                           "finish: waiting for ready_state 3");

    rw_state++;
    while (rw_state != 4) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

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
    quit = 1;
    reader.join();
    writer.join();
  } else {
    // cp->simple_recv();
    auto st = std::make_shared<slope::control::StatusTracker>();
    while (true) {
      auto ptr = cp->poll_migrate(st);
      if (ptr.get() != nullptr) {
        slope::stat::add_value(slope::stat::key::operation,
                               "recieved: object ptr");
        deb(ptr.get()->size());
        ptr.get()->set_lock_vector(&locks);
        std::atomic_int quit = 0;

        std::thread reader([&] {
          std::random_device rd;
          std::mt19937 mt(rd());
          std::uniform_int_distribution<int> cnt(1, 10);
          std::uniform_int_distribution<char> chr('a', 'z');
          std::uniform_int_distribution<int> which(0, 1);
          while (!quit) {
            int rem = 0;
            for (int i = 0; i < gran; i++) {
              auto sz = cnt(mt);
              std::string str;
              for (int j = 0; j < sz; j++) {
                str += chr(mt);
              }
              ptr.get()->get(str);
              rem++;
            }
            slope::stat::add_value(slope::stat::key::operation,
                                   "dst_rem_read:" + std::to_string(rem));
          }
        });

        std::thread writer([&] {
          std::random_device rd;
          std::mt19937 mt(rd());
          std::uniform_int_distribution<int> cnt(1, 10);
          std::uniform_int_distribution<char> chr('a', 'z');
          std::uniform_int_distribution<int> which(0, 1);
          while (!quit) {
            int rem = 0;
            for (int i = 0; i < gran; i++) {
              auto sz = cnt(mt);
              std::string str;
              for (int j = 0; j < sz; j++) {
                str += chr(mt);
              }
              ptr.get()->set(str);
              rem++;
            }
            slope::stat::add_value(slope::stat::key::operation,
                                   "dst_rem_write:" + std::to_string(rem));
          }
        });

        while (st->get_value() != slope::control::StatusTracker::Status::done) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        quit = 1;
        reader.join();
        writer.join();
        break;
      }
    }
    debout("done");
  }
}

}  // namespace bloomfilter
}  // namespace bench
}  // namespace slope
