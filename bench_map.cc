#include "bench_map.h"

#include <limits>
#include <random>
#include <type_traits>

#include "allocator.h"
#include "logging.h"
#include "memcached_kv.h"
#include "mig.h"
#include "rdma_control.h"
#include "stat.h"

namespace slope {
namespace bench {
namespace map {

static const int gran = 1000;

void run(std::string self_id, std::vector<std::string> peers,
         std::unique_ptr<slope::keyvalue::KeyValuePrefixMiddleware> kv,
         std::map<std::string, std::string> params) {
  std::sort(peers.begin(), peers.end());

  auto cp = std::make_unique<slope::control::RdmaControlPlane<BMap>>(
      self_id, peers, std::move(kv));

  assert(params.size() == 1);
  size_t map_size = std::stoul(params[map_size_param]);
  assert(map_size > 0);
  slope::stat::set_param_meta(map_size_param, params[map_size_param]);

  size_t prefill_size = map_size / 100;

  BInt result = 0;
  if (cp->self_name() == cp->cluster_nodes().front()) {
    slope::mig_ptr<BMap> ptr, local;

    // for (size_t i = 0; i < map_size; i++) {
    //   {
    //     auto lock = ptr.create_context();
    //     (*ptr.get())[i] = i * i;
    //   }
    //   for (auto it : *(ptr.get())) {
    //     deb(it.first);
    //     deb(it.second);
    //     std::cout << std::endl;
    //   }
    //   for (auto it : ptr.get_chunks()) {
    //     std::stringstream out;
    //     out << std::showbase << std::internal << std::setfill('0') << " @"
    //         << std::hex << std::setw(16) << it.first;
    //     out << " " << it.second;
    //     debout(out.str());
    //   }
    //   std::cout << std::endl;
    //   for (auto it : ptr.get_pages()) {
    //     std::stringstream out;
    //     out << std::showbase << std::internal << std::setfill('0') << " @"
    //         << std::hex << std::setw(16) << it.first;
    //     out << " " << it.second;
    //     debout(out.str());
    //   }
    //   debline();
    // }

    // deb(ptr.get()->size());
    // for (auto it : ptr.get_chunks()) {
    //   std::stringstream out;
    //   out << std::showbase << std::internal << std::setfill('0') << " @"
    //       << std::hex << std::setw(16) << it.first;
    //   out << " " << it.second;
    //   debout(out.str());
    // }

    std::atomic_int rw_state = -2;
    std::atomic_int quit = 0;

    {
      auto ctx = ptr.create_context();
      for (size_t i = 0; i < prefill_size; i++) {
        (*ptr.get())[i] = i;
      }
      }
      {
        auto ctx = local.create_context();
        for (size_t i = 0; i < prefill_size; i++) {
          (*local.get())[i] = i;
        }
      }

    std::thread worker([&] {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<BInt> val(1, map_size - 1);
      std::uniform_int_distribution<int> op(1, 3);
      std::uniform_int_distribution<BInt> existing_key(0, prefill_size - 1);
      std::uniform_int_distribution<int> table(0, 1);
      int operations = 3;
      while (!quit) {
        if (rw_state == -1) {
          rw_state++;
          operations -= 1;
          // no new write
        }
        if (rw_state == 1) {
          rw_state++;
          operations -= 1;
          // no write
          // no read
        }
        if (rw_state == 3) {
          rw_state++;
          operations -= 1;
        }
        int cnt_ptr[4] = {0};
        int cnt_local[4] = {0};

        for (int t = 0; t < gran; t++) {
          auto this_op = op(mt);
          auto this_table = table(mt);
          auto &current =
              (this_op <= operations && this_table == 0) ? ptr : local;

          auto *stats =
              (this_op <= operations && this_table == 0) ? cnt_ptr : cnt_local;

          if (this_op == 3) {
            auto ctx = current.create_context();
            (*current.get())[val(mt)] = val(mt);
            stats[this_op] += 1;
          } else if (this_op == 2) {  // write existing
            auto key = existing_key(mt);
            auto it =
                const_cast<decltype(current.get())>(current.get())->find(key);
            assert(it !=
                   const_cast<decltype(current.get())>(current.get())->end());
            it->second = val(mt);
            stats[this_op] += 1;
          } else if (this_op == 1) {  // read
            auto it = current.get()->find(val(mt));
            if (it != current.get()->end()) {
              result ^= it->second;
            }
            stats[this_op] += 1;
          }
        }

        for (int i = 1; i <= 3; i++) {
          slope::stat::add_value(slope::stat::key::operation,
                                 "src_ptr_ops_" + std::to_string(i) + ":" +
                                     std::to_string(cnt_ptr[i]));
          slope::stat::add_value(slope::stat::key::operation,
                                 "src_local_ops_" + std::to_string(i) + ":" +
                                     std::to_string(cnt_local[i]));
        }
      }
    });

    auto advance_state = [&] {
      int bef = rw_state;
      rw_state++;
      while (rw_state != bef + 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    };

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    slope::stat::add_value(slope::stat::key::operation,
                           "advance: no new writes");
    advance_state();

    debout("start the migration");
    slope::stat::add_value(slope::stat::key::operation, "call:init_migration");
    slope::stat::add_value(slope::stat::key::operation,
                           "map sz = " + std::to_string(ptr.get()->size()));
    auto operation = cp->init_migration(cp->cluster_nodes()[1], ptr);

    slope::stat::add_value(slope::stat::key::operation,
                           "start: waiting for ready_state 1");
    while (operation->get_ready_state() != 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    slope::stat::add_value(slope::stat::key::operation,
                           "finish: waiting for ready_state 1");

    advance_state();

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

    advance_state();

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
    worker.join();
  } else {
    // cp->simple_recv();
    volatile std::atomic_int quit = 0;
    volatile std::atomic_int done = 0;
    auto st = std::make_shared<slope::control::StatusTracker>();
    while (true) {
      auto ptr = cp->poll_migrate(st);
      if (ptr.get() != nullptr) {
        slope::stat::add_value(slope::stat::key::operation,
                               "recieved: object ptr");
        std::thread worker([&] {
          std::random_device rd;
          std::mt19937 mt(rd());
          std::uniform_int_distribution<BInt> val(1, map_size - 1);
          std::uniform_int_distribution<int> op(1, 3);
          std::uniform_int_distribution<BInt> existing_key(0, prefill_size - 1);
          std::uniform_int_distribution<int> table(0, 1);
          while (!quit) {
            int cnt_ptr[4] = {0};

            for (int t = 0; t < gran; t++) {
              auto this_op = op(mt);
              auto &current = ptr;

              auto *stats = cnt_ptr;
              stats[this_op] += 1;

              if (this_op == 3) {
                auto ctx = current.create_context();
                (*current.get())[val(mt)] = val(mt);
              } else if (this_op == 2) {  // write existing
                auto key = existing_key(mt);
                auto it = const_cast<decltype(current.get())>(current.get())
                              ->find(key);
                assert(
                    it !=
                    const_cast<decltype(current.get())>(current.get())->end());
                it->second = val(mt);
              } else {  // read
                auto it = current.get()->find(val(mt));
                if (it != current.get()->end()) {
                  result ^= it->second;
                }
              }
            }

            for (int i = 1; i <= 3; i++) {
              slope::stat::add_value(slope::stat::key::operation,
                                     "dst_ptr_ops_" + std::to_string(i) + ":" +
                                         std::to_string(cnt_ptr[i]));
            }
          }
        });

        while (st->get_value() != slope::control::StatusTracker::Status::done) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        quit = 1;
        worker.join();
        break;
      }
    }
    debout("done");
  }
}

}  // namespace map
}  // namespace bench
}  // namespace slope
