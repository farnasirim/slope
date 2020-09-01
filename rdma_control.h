#ifndef SLOPE_RDMA_CONTROL_H_
#define SLOPE_RDMA_CONTROL_H_

#include <arpa/inet.h>
#include <malloc.h>

#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "allocator.h"
#include "cluster_time.h"
#include "control.h"
#include "data.h"
#include "ib.h"
#include "ib_container.h"
#include "json.hpp"
#include "keyvalue.h"
#include "mig.h"
#include "modify_qp.h"
#include "page_tracker.h"
#include "sig.h"
#include "stat.h"
#include "thread_joiner.h"

namespace slope {
namespace control {

using json = nlohmann::json;

enum class wrid : uint64_t {
  calibrate_time = 0xd018,
  chunks_info,
  received_chunks,
  destination_rkeys,
  prefill_page,
  dirty_pages_count,
  dirty_pages,
  dirty_rkeys,
  read_dirty_page,
  final_confirmation,
};

struct QpInfo {
  QpInfo();
  QpInfo(short unsigned int host_port_lid, unsigned int host_qp_num,
         const std::string &host_node_id,
         const std::string &remote_end_node_id);
  short unsigned int host_port_lid;
  unsigned int host_qp_num;
  std::string host_node_id;
  std::string remote_end_node_id;
};

struct NodeInfo {
  NodeInfo() = default;
  NodeInfo(std::string node_id,
           const std::map<std::string, std::vector<QpInfo>> &qps);
  std::string node_id;
  std::map<std::string, std::vector<QpInfo>> qp_sets_;
};

extern "C" {

struct DoMigrateRequest {
  size_t number_of_chunks;
};

struct DoMigrateChunk {
  uintptr_t addr;
  size_t sz;
};

struct Page {
  uintptr_t addr;
  size_t sz;
};
}

class TwoStepMigrationOperation : public MigrationOperation {
 public:
  bool try_commit() override;
  TwoStepMigrationOperation(std::shared_ptr<std::mutex> m,
                            std::shared_ptr<int> ready_state,
                            std::shared_ptr<std::condition_variable> cv,
                            std::thread t);

  void collect() override;
  int get_ready_state() override;
  ~TwoStepMigrationOperation();

 private:
  std::shared_ptr<std::mutex> m_;
  std::shared_ptr<int> ready_state_;
  std::shared_ptr<std::condition_variable> cv_;

  std::thread t_;
};

class FullMeshQpSet {
 public:
  FullMeshQpSet();
  FullMeshQpSet(const IbvCreateCq &cq);
  std::vector<QpInfo> prepare(const std::string &excl,
                              const std::vector<std::string> &nodes,
                              const IbvAllocPd &pd,
                              struct ibv_port_attr &port_attrs,
                              const struct ibv_device_attr &dev_attrs,
                              int sig_all);
  void finalize(const std::vector<QpInfo> &remote_qps);

  const IbvCreateCq &cq_;
  std::map<std::string, std::shared_ptr<IbvCreateQp>> qps_;
  std::map<std::string, std::shared_ptr<ibv_qp_init_attr>> qp_attrs_;
};

void to_rts(IbvCreateQp &, QpInfo);

void to_json(json &j, const NodeInfo &inf) noexcept;
void from_json(const json &j, NodeInfo &inf) noexcept;
void to_json(json &j, const QpInfo &inf) noexcept;
void from_json(const json &j, QpInfo &inf) noexcept;

std::string time_point_to_string(
    const std::chrono::high_resolution_clock::time_point &);

template <typename T>
class RdmaControlPlane : public ControlPlane<T> {
 private:
  ds::ThreadJoiner joiner_;

 public:
  const std::string self_name_;
  size_t self_index_;
  std::vector<std::string> cluster_nodes_;

  using ptr = std::unique_ptr<RdmaControlPlane>;

  uint32_t get_nudge(ibv_qp *qp, ibv_cq *cq) {
    struct ibv_recv_wr *bad_wr;
    struct ibv_recv_wr this_wr = {};
    this_wr.wr_id = 12212;
    this_wr.num_sge = 0;
    this_wr.sg_list = nullptr;

    int ret_post_recv = ibv_post_recv(qp, &this_wr, &bad_wr);
    assert_p(ret_post_recv == 0, "ibv_post_recv");

    struct ibv_wc wc[1];
    while (true) {
      int ret_poll_cq = ibv_poll_cq(cq, 1, wc);
      assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "ibv_poll_cq");
      if (ret_poll_cq > 0) {
        assert_ibv_completion(wc[0].status);
        assert(wc[0].wr_id == 12212);
        debout("got nudge");
        deb(ntohl(wc[0].imm_data));
        return ntohl(wc[0].imm_data);
      }
    }
    return 0;
  }

  void do_nudge(ibv_qp *qp, uint32_t imm, ibv_cq *cq) {
    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};
    this_wr.wr_id = 12212;
    this_wr.num_sge = 0;
    this_wr.sg_list = nullptr;
    this_wr.opcode = IBV_WR_SEND_WITH_IMM;
    this_wr.send_flags = IBV_SEND_SIGNALED;
    this_wr.imm_data = htonl(imm);

    int ret_post_send = ibv_post_send(qp, &this_wr, &bad_wr);
    assert_p(ret_post_send == 0, "ibv_post_send");
    struct ibv_wc completions[1];

    while (true) {
      int ret = ibv_poll_cq(cq, 1, completions);
      if (ret > 0) {
        assert_ibv_completion(completions[0].status);
        assert(completions[0].wr_id == 12212);
        break;
      }
    }
    // send_wr.wr.rdma.remote_addr = reinterpret_cast<uintptr_t>(mr->addr);
    // send_wr.wr.rdma.rkey = rkey;
  }

  uint32_t get_nudge_write(ibv_qp *qp, ibv_cq *cq) {
    struct ibv_recv_wr *bad_wr;
    struct ibv_recv_wr this_wr = {};
    this_wr.wr_id = 12212;
    this_wr.num_sge = 0;
    this_wr.sg_list = nullptr;

    int ret_post_recv = ibv_post_recv(qp, &this_wr, &bad_wr);
    assert_p(ret_post_recv == 0, "ibv_post_recv");

    struct ibv_wc wc[1];
    while (true) {
      int ret_poll_cq = ibv_poll_cq(cq, 1, wc);
      assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "ibv_poll_cq");
      if (ret_poll_cq > 0) {
        assert_ibv_completion(wc[0].status);
        assert(wc[0].wr_id == 12212);
        debout("got nudge");
        deb(ntohl(wc[0].imm_data));
        return ntohl(wc[0].imm_data);
      }
    }
    return 0;
  }

  void do_nudge_write(ibv_qp *qp, ibv_cq *cq, uintptr_t remote_addr,
                      uint32_t rkey, void *send_buf, uint32_t send_buf_len,
                      uint32_t send_buf_lkey) {
    struct ibv_sge sge = {};
    sge.addr = reinterpret_cast<uintptr_t>(send_buf);
    sge.length = send_buf_len;
    sge.lkey = send_buf_lkey;

    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};

    this_wr.wr_id = 12212;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;
    this_wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    this_wr.send_flags = IBV_SEND_SIGNALED;
    this_wr.imm_data = htonl(123123);
    this_wr.wr.rdma.remote_addr = remote_addr;
    this_wr.wr.rdma.rkey = rkey;

    int ret_post_send = ibv_post_send(qp, &this_wr, &bad_wr);
    assert_p(ret_post_send == 0, "ibv_post_send");
    struct ibv_wc completions[1];

    while (true) {
      int ret = ibv_poll_cq(cq, 1, completions);
      if (ret > 0) {
        assert_ibv_completion(completions[0].status);
        assert(completions[0].wr_id == 12212);
        break;
      }
    }
  }

  void read_page(uintptr_t page_addr, uint32_t sz, uint32_t lkey, ibv_qp *qp,
                 uint32_t rkey, uint64_t wrid, ibv_cq *cq) {
    struct ibv_sge sge = {};
    sge.addr = reinterpret_cast<uintptr_t>(page_addr);
    sge.length = sz;
    sge.lkey = lkey;

    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};

    this_wr.wr_id = wrid;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;
    this_wr.opcode = IBV_WR_RDMA_READ;
    this_wr.send_flags = IBV_SEND_SIGNALED;  // unsignaled
    this_wr.wr.rdma.remote_addr = page_addr;
    this_wr.wr.rdma.rkey = rkey;

    auto ret_send = ibv_post_send(qp, &this_wr, &bad_wr);
    assert_p(ret_send == 0, "ibv_post_send");

    while (true) {
      struct ibv_wc wc[1] = {};
      int ret_poll_cq = ibv_poll_cq(cq, 1, wc);
      assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "ibv_poll_cq");
      if (ret_poll_cq > 0) {
        assert_ibv_completion(wc[0].status);
        assert(wc[0].wr_id == wrid);
        break;
      }
    }
  }

  void write_page(uintptr_t page_addr, uint32_t sz, uint32_t lkey, ibv_qp *qp,
                  uint32_t rkey, uint64_t wrid, ibv_cq *cq) {
    struct ibv_sge sge = {};
    sge.addr = reinterpret_cast<uintptr_t>(page_addr);
    sge.length = sz;
    sge.lkey = lkey;

    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};

    this_wr.wr_id = wrid;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;
    this_wr.opcode = IBV_WR_RDMA_WRITE;
    this_wr.send_flags = IBV_SEND_SIGNALED;
    this_wr.wr.rdma.remote_addr = page_addr;
    this_wr.wr.rdma.rkey = rkey;

    auto ret_send = ibv_post_send(qp, &this_wr, &bad_wr);
    assert_p(ret_send == 0, "ibv_post_send");

    while (true) {
      struct ibv_wc wc[1] = {};
      int ret_poll_cq = ibv_poll_cq(cq, 1, wc);
      assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "ibv_poll_cq");
      if (ret_poll_cq > 0) {
        assert_p(wc[0].status == 0, "ibv_poll_cq");
        assert(wc[0].wr_id == wrid);
        break;
      }
    }
  }

    virtual MigrationOperation::ptr init_migration(
        const std::string &dest, const mig_ptr<T> &target_object) {
      if (!keyvalue_service_->compare_and_swap(migrate_in_progress_cas_name_,
                                               "0", "1")) {
        return nullptr;
      }
      slope::stat::add_value(slope::stat::key::operation,
                             "start init_migration");
      auto chunks = target_object.get_chunks();

      auto ready_state_mutex = std::make_shared<std::mutex>();
      std::shared_ptr<int> ready_state = std::make_shared<int>();
      *ready_state = 0;
      auto ready_state_cv = std::make_shared<std::condition_variable>();

      return std::make_unique<TwoStepMigrationOperation>(
          ready_state_mutex, ready_state, ready_state_cv,
          std::thread([this, dest, chunks, ready_state_mutex, ready_state,
                       ready_state_cv] {
            std::lock_guard<std::mutex> polling_guard(
                control_plane_polling_lock_);
            auto remote_rkeys = start_migrate_ping_pong(dest, chunks);
            slope::stat::add_value(slope::stat::key::operation,
                                   "got destination rkeys");
            auto pages = slope::alloc::chunks_to_pages(chunks);
            slope::stat::set_meta(slope::stat::metakey::num_pages,
                                  std::to_string(pages.size()));
            assert(pages.size() == remote_rkeys.size());
            auto dest_qp =
                fullmesh_qps_[shared_address_qps_key_].qps_[dest].get()->get();
            auto &cq = fullmesh_qps_[do_migrate_qps_key_].cq_;

            std::vector<IbvRegMr> source_mrs;

            for (size_t i = 0; i < pages.size(); i++) {
              auto rkey = remote_rkeys[i];
              auto page = pages[i];
              sig::add_dirty_detection(page.first);
              if (mprotect(reinterpret_cast<void *>(page.first), page.second,
                           PROT_READ)) {
                perror("mprotect");
                assert(false);
              }
              source_mrs.emplace_back(global_pd_,
                                      reinterpret_cast<void *>(page.first),
                                      page.second, 0);
              write_page(page.first, page.second, source_mrs.back()->lkey,
                         dest_qp, rkey,
                         static_cast<uint64_t>(wrid::prefill_page), cq.get());
              sig::finish_transfer(page.first);
            }
            slope::stat::add_value(slope::stat::key::operation,
                                   "finish prefill");

            debout("will try the lock");
            {
              std::lock_guard<std::mutex> lk(*ready_state_mutex);
              debout("in lock");
              *ready_state = 1;
            }
            slope::stat::add_value(slope::stat::key::operation,
                                   "ready to commit");
            {
              std::unique_lock<std::mutex> ul(*ready_state_mutex);
              debout("will wait for 2");
              ready_state_cv->wait(
                  ul, [&ready_state] { return *ready_state == 2; });
              debout("got to 2");
            }
            slope::stat::add_value(slope::stat::key::operation,
                                   "object locked - proceed to commit");
            // TODO: get dirty pages
            std::vector<Page> dirty_pages;
            std::vector<IbvRegMr> dirty_mrs;
            for (auto it : pages) {
              // deb(it.first);
              // deb(sig::is_dirty(it.first));
              if (sig::is_dirty(it.first)) {
                Page p;
                p.addr = it.first;
                p.sz = it.second;
                dirty_pages.push_back(p);
              }
            }
            sig::remove_dirty_detection();
            deb(dirty_pages.size());
            slope::stat::add_value(slope::stat::key::operation,
                                   "got dirty pages");
            slope::stat::set_meta(slope::stat::metakey::num_dirty_pages,
                                  std::to_string(dirty_pages.size()));

            send_imm(static_cast<uint32_t>(dirty_pages.size()),
                     static_cast<uint64_t>(wrid::dirty_pages_count), dest_qp,
                     do_migrate_cq_.get());
            if (!dirty_pages.empty()) {
              send_vector(dirty_pages, static_cast<uint64_t>(wrid::dirty_pages),
                          dest_qp, do_migrate_cq_.get());
              for (auto &page : dirty_pages) {
                dirty_mrs.emplace_back(global_pd_,
                                       reinterpret_cast<void *>(page.addr),
                                       page.sz, final_read_mr_flags_);
              }
              std::vector<uint32_t> dirty_rkeys;
              std::transform(
                  dirty_mrs.begin(), dirty_mrs.end(),
                  std::back_inserter(dirty_rkeys),
                  [&](auto &current_mr) { return current_mr.get()->rkey; });
              send_vector(dirty_rkeys, static_cast<uint64_t>(wrid::dirty_rkeys),
                          dest_qp, do_migrate_cq_.get());
            }
            slope::stat::add_value(
                slope::stat::key::operation,
                "hand over the ownership to the destination");

            assert(final_confirmation_value_ ==
                   recv_imm(static_cast<uint64_t>(wrid::final_confirmation),
                            dest_qp, do_migrate_cq_.get()));

            slope::stat::add_value(
                slope::stat::key::operation,
                "final confirmation received from the destination");

            // for(auto& chunk: chunks) {
            //   deb(chunk);
            //   // auto mprotect_result = mprotect(
            //   //     reinterpret_cast<void*>(chunk.first),
            //   //     chunk.second, PROT_READ);
            //   // assert_p(mprotect_result == 0, "mprotect");
            // }

            // transfer_ownership_ping_pong(dest, chunks);
            ready_state_mutex->unlock();

            assert(keyvalue_service_->compare_and_swap(
                migrate_in_progress_cas_name_, "1", "0"));
          }));
    }

    // void calibrate_time();

    RdmaControlPlane(const std::string &self_name,
                     const std::vector<std::string> &cluster_nodes,
                     slope::keyvalue::KeyValueService::ptr keyvalue_service,
                     int calibrate_time = 0)
        : self_name_(self_name),
          cluster_nodes_(cluster_nodes),
          keyvalue_service_(std::move(keyvalue_service)),
          dataplane_(nullptr),
          ib_context_(ib_device_name_.c_str()),
          dev_attrs_result_(ibv_query_device(ib_context_.get(), &dev_attrs_)),
          query_port_result_(ibv_query_port(
              ib_context_.get(), operating_port_num_, &operating_port_attr_)),
          global_pd_(ib_context_.get()),
          do_migrate_mr_(global_pd_.get(), &do_migrate_req_,
                         sizeof(do_migrate_req_), do_migrate_mr_flags_),
          do_migrate_cq_(ib_context_.get(), dev_attrs_.max_cqe,
                         static_cast<void *>(NULL),
                         static_cast<struct ibv_comp_channel *>(NULL), 0),
          time_calib_cq_(ib_context_.get(), dev_attrs_.max_cqe,
                         static_cast<void *>(NULL),
                         static_cast<struct ibv_comp_channel *>(NULL), 0) {
      std::sort(cluster_nodes_.begin(), cluster_nodes_.end());
      self_index_ = static_cast<size_t>(
          std::find(cluster_nodes_.begin(), cluster_nodes_.end(), self_name_) -
          cluster_nodes_.begin());

      if (is_leader()) {
        init_cluster();
      }

      std::map<std::string, std::vector<QpInfo>> all_qps;
      std::vector<std::string> qp_set_keys = {
          do_migrate_qps_key_,
          shared_address_qps_key_,
      };
      if (calibrate_time) {
        qp_set_keys.push_back(time_calib_qps_key_);
      }

      for (auto set_name : qp_set_keys) {
        fullmesh_qps_.try_emplace(set_name, get_cq(set_name));
        auto qps = fullmesh_qps_[set_name].prepare(
            self_name_, cluster_nodes_, global_pd_, operating_port_attr_,
            dev_attrs_, set_name != shared_address_qps_key_);
        all_qps[set_name] = qps;
      }

      NodeInfo my_info(self_name_, all_qps);
      // deb(json(my_info).dump(4));
      my_info.node_id = self_name_;

      keyvalue_service_->set(self_name_, json(my_info).dump());
      for (auto peer : cluster_nodes_) {
        std::string peer_info;
        auto peer_result = keyvalue_service_->wait_for(peer, peer_info);
        assert(peer_result);
        cluster_info_[peer] = json::parse(peer_info).get<NodeInfo>();
      }

      for (auto set_name : qp_set_keys) {
        auto remote_qps = find_self_qps(cluster_info_, set_name);
        fullmesh_qps_[set_name].finalize(remote_qps);
      }

      for (auto &qp_ptr : fullmesh_qps_[do_migrate_qps_key_].qps_) {
        auto qp = qp_ptr.second.get()->get();
        prepost_do_migrate_qp(qp);
      }

      if (calibrate_time && !is_leader()) {
        post_calibrate_time(get_leader());
      }

      keyvalue_service_->set(self_name_ + "_DONE", json(my_info).dump());
      for (auto peer : cluster_nodes_) {
        std::string _;
        assert(keyvalue_service_->wait_for(peer_done_key(peer), _));
      }

      if (calibrate_time) {
        do_calibrate_time();
      }
    }

    std::string get_leader() {
      return *min_element(cluster_nodes_.begin(), cluster_nodes_.end());
    }

    bool is_leader() { return self_name_ == get_leader(); }

    bool is_leader(const std::string &name) { return name == get_leader(); }

    void post_calibrate_time(const std::string &remote_node) {
      ibv_recv_wr calibrate_time_wr_ = {};
      ibv_recv_wr *calibrate_time_bad_wr_ = nullptr;
      calibrate_time_wr_.wr_id = calibrate_time_wrid_;
      calibrate_time_wr_.num_sge = 0;
      calibrate_time_wr_.sg_list = nullptr;
      int ret_post_recv = ibv_post_recv(
          fullmesh_qps_[time_calib_qps_key_].qps_[remote_node].get()->get(),
          &calibrate_time_wr_, &calibrate_time_bad_wr_);
      assert_p(ret_post_recv == 0, "ibv_post_recv");
    }

    void do_calibrate_time_follower() {
      auto leader_qp =
          fullmesh_qps_[time_calib_qps_key_].qps_[get_leader()].get()->get();
      auto &cq = fullmesh_qps_[time_calib_qps_key_].cq_;
      std::vector<std::chrono::high_resolution_clock::time_point> time_points;
      for (int i = 0; i < time_calib_rounds_; i++) {
        post_calibrate_time(get_leader());
        while (true) {
          struct ibv_wc completions[1];
          int ret = ibv_poll_cq(cq.get(), 1, completions);
          if (ret > 0) {
            time_points.push_back(
                std::chrono::high_resolution_clock::now() -
                std::chrono::microseconds(ntohl(completions[0].imm_data)));
            assert_ibv_completion(completions[0].status);
            break;
          }
        }

        struct ibv_send_wr *bad_wr;
        struct ibv_send_wr this_wr = {};
        this_wr.wr_id = calibrate_time_wrid_;
        this_wr.num_sge = 0;
        this_wr.sg_list = NULL;
        this_wr.opcode = IBV_WR_SEND_WITH_IMM;
        this_wr.send_flags = IBV_SEND_SIGNALED;
        this_wr.imm_data = htonl(self_index_);
        int ret_post_send = ibv_post_send(leader_qp, &this_wr, &bad_wr);
        assert_p(ret_post_send == 0, "ibv_post_send");

        while (true) {
          struct ibv_wc completions[1];
          int ret = ibv_poll_cq(cq.get(), 1, completions);
          if (ret > 0) {
            assert_ibv_completion(completions[0].status);
            break;
          }
        }
      }
      int selected_ind = time_points.size() / 2;
      std::nth_element(time_points.begin(), time_points.begin() + selected_ind,
                       time_points.end());
      slope::time::calibrated_local_start_time =
          *(time_points.begin() + selected_ind);
    }
    void do_calibrate_time_leader() {
      slope::time::calibrated_local_start_time =
          std::chrono::high_resolution_clock::now();
      // deb(time_point_to_string(slope::time::calibrated_local_start_time));
      // deb(time_point_to_string(slope::time::calibrated_local_start_time));
      for (auto dest : cluster_nodes_) {
        if (is_leader(dest)) {
          continue;
        }
        int current_node_rtt_micros = 0;
        for (int i = 0; i < time_calib_rounds_; i++) {
          auto dest_qp =
              fullmesh_qps_[time_calib_qps_key_].qps_[dest].get()->get();
          auto &cq = fullmesh_qps_[time_calib_qps_key_].cq_;

          struct ibv_send_wr *bad_wr;
          struct ibv_send_wr this_wr = {};
          this_wr.wr_id = calibrate_time_wrid_;
          this_wr.num_sge = 0;
          this_wr.sg_list = NULL;
          this_wr.opcode = IBV_WR_SEND_WITH_IMM;
          this_wr.send_flags = IBV_SEND_SIGNALED;
          auto current_node_offset_micros =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::high_resolution_clock::now() -
                  slope::time::calibrated_local_start_time)
                  .count();
          this_wr.imm_data =
              htonl(current_node_rtt_micros / 2 + current_node_offset_micros);

          post_calibrate_time(dest);

          auto then = std::chrono::high_resolution_clock::now();
          int ret_post_send = ibv_post_send(dest_qp, &this_wr, &bad_wr);
          assert_p(ret_post_send == 0, "ibv_post_send");
          while (true) {
            struct ibv_wc completions[1];
            int ret = ibv_poll_cq(cq.get(), 1, completions);
            if (ret > 0) {
              assert_ibv_completion(completions[0].status);
              break;
            }
          }

          while (true) {
            struct ibv_wc completions[1];
            int ret = ibv_poll_cq(cq.get(), 1, completions);
            if (ret > 0) {
              assert_ibv_completion(completions[0].status);
              break;
            }
          }
          current_node_rtt_micros =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::high_resolution_clock::now() - then)
                  .count();
        }
      }
    }
    void do_calibrate_time() {
      if (is_leader()) {
        do_calibrate_time_leader();
      } else {
        do_calibrate_time_follower();
      }

      slope::stat::add_value(slope::stat::key::operation,
                             slope::stat::value::done_time_calibrate);
    }

    IbvCreateCq &get_cq(const std::string &str) {
      if (str == time_calib_qps_key_) {
        return time_calib_cq_;
      }
      return do_migrate_cq_;
    }

    template <typename VT>
    std::vector<VT> recv_vector(size_t num, uint64_t wrid, ibv_qp * peer_qp,
                                ibv_cq * cq) {
      std::vector<VT> ret(num);
      auto data = ret.data();
      auto data_sz = ret.size() * sizeof(ret[0]);
      IbvRegMr mr(global_pd_.get(), data, data_sz, do_migrate_mr_flags_);

      {
        struct ibv_sge sge = {};
        sge.lkey = mr->lkey;
        sge.addr = reinterpret_cast<uint64_t>(data);
        sge.length = data_sz;

        struct ibv_recv_wr *bad_wr;
        struct ibv_recv_wr this_wr = {};
        this_wr.wr_id = wrid;
        this_wr.num_sge = 1;
        this_wr.sg_list = &sge;

        int ret_post_recv = ibv_post_recv(peer_qp, &this_wr, &bad_wr);
        assert_p(ret_post_recv == 0, "ibv_post_recv");
      }

      {
        struct ibv_wc wc[1];
        while (true) {
          int ret_poll_cq = ibv_poll_cq(cq, 1, wc);
          assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "ibv_poll_cq");
          if (ret_poll_cq > 0) {
            assert_ibv_completion(wc[0].status);
            assert(wc[0].wr_id == wrid);
            break;
          }
        }
      }
      return ret;
    }

    void send_imm(uint32_t val, uint64_t wrid, ibv_qp * dest_qp, ibv_cq * cq) {
      debout("Send");
      deb(val);
      deb(wrid);
      debout("");

      struct ibv_send_wr *bad_wr;
      struct ibv_send_wr this_wr = {};
      this_wr.wr_id = wrid;
      this_wr.num_sge = 0;
      this_wr.sg_list = nullptr;
      this_wr.opcode = IBV_WR_SEND_WITH_IMM;
      this_wr.send_flags = IBV_SEND_SIGNALED;
      this_wr.imm_data = htonl(val);

      int ret_post_send = ibv_post_send(dest_qp, &this_wr, &bad_wr);
      assert_p(ret_post_send == 0, "ibv_post_send");
      struct ibv_wc completions[1];


      while (true) {
        int ret = ibv_poll_cq(cq, 1, completions);
        if (ret > 0) {
          assert_ibv_completion(completions[0].status);
          assert(completions[0].wr_id == wrid);
          break;
        }
      }
    }

    uint32_t recv_imm(uint64_t wrid, ibv_qp * peer_qp, ibv_cq * cq) {
      debout("Recv");
      deb(wrid);
      debout("");

      struct ibv_recv_wr *bad_wr;
      struct ibv_recv_wr this_wr = {};
      this_wr.wr_id = wrid;
      this_wr.num_sge = 0;
      this_wr.sg_list = nullptr;
      int ret_post_recv = ibv_post_recv(peer_qp, &this_wr, &bad_wr);
      assert_p(ret_post_recv == 0, "ibv_post_recv");
      struct ibv_wc wc[1];
      while (true) {
        int ret_poll_cq = ibv_poll_cq(cq, 1, wc);
        assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "ibv_poll_cq");
        if (ret_poll_cq > 0) {
          assert_ibv_completion(wc[0].status);
          assert(wc[0].wr_id == wrid);
          return ntohl(wc[0].imm_data);
          break;
        }
      }
      return 0;
    }

    template <typename VT>
    void send_vector(std::vector<VT> & v, uint64_t wrid, ibv_qp * dest_qp,
                     ibv_cq * cq) {
      auto data = v.data();
      auto data_sz = v.size() * sizeof(v[0]);
      IbvRegMr mr(global_pd_.get(), data, data_sz, do_migrate_mr_flags_);

      struct ibv_sge sge = {};
      sge.lkey = mr.get()->lkey;
      sge.addr = reinterpret_cast<uintptr_t>(data);
      sge.length = data_sz;

      struct ibv_send_wr *bad_wr;
      struct ibv_send_wr this_wr = {};
      this_wr.wr_id = wrid;
      this_wr.num_sge = 1;
      this_wr.sg_list = &sge;
      this_wr.opcode = IBV_WR_SEND_WITH_IMM;
      this_wr.send_flags = IBV_SEND_SIGNALED;
      this_wr.imm_data = htonl(self_index_);

      int ret_post_send = ibv_post_send(dest_qp, &this_wr, &bad_wr);
      assert_p(ret_post_send == 0, "ibv_post_send");
      struct ibv_wc completions[1];

      while (true) {
        int ret = ibv_poll_cq(cq, 1, completions);
        if (ret > 0) {
          assert_ibv_completion(completions[0].status);
          assert(completions[0].wr_id == wrid);
          break;
        }
      }
    }

    mig_ptr<T> poll_migrate() {
      // TODO: repost the recvs to do_migrate_qp
      std::lock_guard<std::mutex> polling_guard(control_plane_polling_lock_);

      size_t peer_index;
      {
        struct ibv_wc completions[1];
        int ret_poll_cq = ibv_poll_cq(do_migrate_cq_, 1, completions);
        assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1,
                 "poll_migrate: ibv_poll_cq");
        if (ret_poll_cq == 0) {
          return mig_ptr<T>::adopt(nullptr);
        }

        deb(do_migrate_req_.number_of_chunks);
        peer_index = ntohl(completions[0].imm_data);
      }

      slope::stat::add_value(slope::stat::key::operation,
                             "received migration request from source");

      deb(peer_index);
      ibv_qp *peer_qp = fullmesh_qps_[shared_address_qps_key_]
                            .qps_[cluster_nodes_[peer_index]]
                            .get()
                            ->get();

      std::vector<DoMigrateChunk> chunks_info(do_migrate_req_.number_of_chunks);
      auto chunks_info_sz = chunks_info.size() * sizeof(DoMigrateChunk);

      deb(chunks_info_sz);
      deb(chunks_info.size());

      IbvRegMr mr(global_pd_.get(), chunks_info.data(), chunks_info_sz,
                  do_migrate_mr_flags_);

      {
        struct ibv_sge sge = {};
        sge.lkey = mr->lkey;
        sge.addr = reinterpret_cast<uint64_t>(chunks_info.data());
        sge.length = chunks_info_sz;

        struct ibv_recv_wr *bad_wr;
        struct ibv_recv_wr this_wr = {};
        this_wr.wr_id = chunks_info_wrid_;
        this_wr.num_sge = 1;
        this_wr.sg_list = &sge;

        int ret = ibv_post_recv(peer_qp, &this_wr, &bad_wr);
        assert_p(ret == 0, "ibv_post_recv");
      }

      {
        struct ibv_wc chunks_completions[1];
        while (true) {
          int chunks_ret_poll_cq =
              ibv_poll_cq(do_migrate_cq_, 1, chunks_completions);
          assert_p(chunks_ret_poll_cq >= 0 && chunks_ret_poll_cq <= 1,
                   "ibv_poll_cq");
          if (chunks_ret_poll_cq > 0) {
            assert_ibv_completion(chunks_completions[0].status);
            assert(chunks_completions[0].wr_id == chunks_info_wrid_);
            break;
          }
        }
      }

      slope::stat::add_value(slope::stat::key::operation,
                             "received object memory address ranges");

      std::vector<slope::alloc::memory_chunk> chunks;
      for (auto [addr, sz] : chunks_info) {
        deb2(addr, sz);
        chunks.emplace_back(addr, sz);
      }
      // {
      //   std::stringstream deb_ss;
      //   deb_ss << std::showbase << std::internal << std::setfill('0')
      //     << "addr: " << std::hex << std::setw(16) <<
      //     static_cast<void*>(chunks_info.data());
      //   infoout(deb_ss.str());
      // }
      // debout("done");

      auto &t_allocator = alloc::allocator_instance<T>();
      // // HUUUUGE TODO: owner is not necessarily the first page
      auto raw = t_allocator.register_preowned(
          *min_element(chunks.begin(), chunks.end()), chunks);

      std::vector<IbvRegMr> mrs;
      auto pages = slope::alloc::chunks_to_pages(chunks);
      for (auto page : pages) {
        mrs.emplace_back(global_pd_, reinterpret_cast<void *>(page.first),
                         page.second, shared_address_mr_flags_);

        if (mprotect(reinterpret_cast<void *>(page.first), page.second,
                     PROT_NONE)) {
          perror("mprotect");
          assert(false);
        }
      }

      joiner_.add(std::thread([this, peer_qp, mrs = std::move(mrs), pages] {
        // for (auto &it : mrs) {
        //   deb(it.get()->addr);
        //   deb(it.get()->rkey);
        // }

        std::vector<uint32_t> local_rkeys;
        std::transform(mrs.begin(), mrs.end(), std::back_inserter(local_rkeys),
                       [&](auto &m) { return m.get()->rkey; });

        // deb(local_rkeys);
        send_vector(local_rkeys, destination_rkeys_wrid_, peer_qp,
                    do_migrate_cq_.get());

        auto num_dirty_pages =
            recv_imm(static_cast<uint64_t>(wrid::dirty_pages_count), peer_qp,
                     do_migrate_cq_.get());

        std::vector<Page> dirty_pages;
        std::vector<uint32_t> dirty_rkeys;
        deb(num_dirty_pages);
        if (num_dirty_pages) {
          dirty_pages = recv_vector<Page>(
              num_dirty_pages, static_cast<uint64_t>(wrid::dirty_pages),
              peer_qp, do_migrate_cq_.get());
          dirty_rkeys = recv_vector<uint32_t>(
              num_dirty_pages, static_cast<uint64_t>(wrid::dirty_rkeys),
              peer_qp, do_migrate_cq_.get());
        }

        slope::stat::add_value(slope::stat::key::operation,
                               "received ownership");

        size_t dirty_page_index = 0;
        slope::ds::PageTracker pt;
        slope::sig::set_active_tracker(&pt);
        for (size_t i = 0; i < pages.size(); i++) {
          auto page = pages[i];
          while (dirty_page_index < num_dirty_pages &&
                 dirty_pages[dirty_page_index].addr < page.first) {
            dirty_page_index++;
          }
          if (dirty_page_index < num_dirty_pages &&
              dirty_pages[dirty_page_index].addr == page.first) {
            pt.add(slope::ds::Page(dirty_pages[dirty_page_index].addr,
                                   dirty_pages[dirty_page_index].sz,
                                   mrs[i].get()->lkey,
                                   dirty_rkeys[dirty_page_index]));
            // deb(dirty_pages[dirty_page_index].addr);
            // deb(dirty_pages[dirty_page_index].sz);
            // deb(mrs[i].get()->lkey);
            // deb(dirty_rkeys[dirty_page_index]);
            continue;
          }
          if (mprotect(reinterpret_cast<void *>(page.first), page.second,
                       PROT_READ | PROT_WRITE)) {
            perror("mprotect");
            assert(false);
          }
        }
        pt.close();

        try {
          while (true) {
            auto page = pt.pop();
            read_page(page.addr, page.sz, page.lkey, peer_qp, page.remote_rkey,
                      static_cast<uint64_t>(wrid::read_dirty_page),
                      do_migrate_cq_.get());
            if (mprotect(reinterpret_cast<void *>(page.addr), page.sz,
                         PROT_READ | PROT_WRITE)) {
              perror("mprotect");
              assert(false);
            }
          }
        } catch (std::exception &) {
        }
        // slope::sig::set_active_tracker(nullptr);
        slope::stat::add_value(slope::stat::key::operation,
                               "read all dirty pages");
        send_imm(final_confirmation_value_,
                 static_cast<uint64_t>(wrid::final_confirmation), peer_qp,
                 do_migrate_cq_.get());
        slope::stat::add_value(slope::stat::key::operation,
                               "sent final confirmation to source");
      }));
      debout("before ret raw");

      slope::stat::add_value(slope::stat::key::operation,
                             "returning object, ready to be referenced");
      debout("calling adopt");
      return mig_ptr<T>::adopt(raw);
    }

    bool init_kvservice() {
      return keyvalue_service_->set(migrate_in_progress_cas_name_, "0");
    }

    void attach_dataplane(slope::data::DataPlane::ptr dataplane) {
      dataplane_ = std::move(dataplane);
    }

    const std::string self_name() final override { return self_name_; }

    const std::vector<std::string> cluster_nodes() final override {
      return cluster_nodes_;
    }

    void simple_send() {
      int machine_id = 0;

      size_t amount_data_bytes_to_req = 100;  // 10GB
      size_t to_alloc = amount_data_bytes_to_req + 10;
      char *mem = static_cast<char *>(memalign(4096, to_alloc));
      size_t *p = reinterpret_cast<decltype(p)>(mem);
      int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                  IBV_ACCESS_REMOTE_WRITE;

      IbvRegMr mr(global_pd_.get(), mem, to_alloc, flags);

      std::cout << "before cq" << std::endl;

      IbvCreateCq cq(ib_context_.get(), dev_attrs_.max_cqe,
                     static_cast<void *>(NULL),
                     static_cast<struct ibv_comp_channel *>(NULL), 0);
      std::cout << "after cq" << std::endl;

      struct ibv_qp_init_attr qp_init_attr = {};
      qp_init_attr.send_cq = cq.get();
      qp_init_attr.recv_cq = cq.get();
      // qp_init_attr.cap.max_send_wr =
      // static_cast<uint33_t>(dev_attrs.max_qp_wr;
      qp_init_attr.cap.max_send_wr = 1;
      // qp_init_attr.cap.max_recv_wr =
      // static_cast<uint33_t>(dev_attrs.max_qp_wr);
      qp_init_attr.cap.max_recv_wr = 1;
      qp_init_attr.cap.max_send_sge = 1;
      qp_init_attr.cap.max_recv_sge = 1;
      // qp_init.cap.max_inline_data = 60;
      qp_init_attr.qp_type = IBV_QPT_RC;

      IbvCreateQp qp(global_pd_.get(), &qp_init_attr);

      auto target_key = std::to_string(1 - machine_id) + "exch";
      auto adv_key = std::to_string(machine_id) + "exch";

      deb(adv_key);
      deb(target_key);

      auto local_qp = QpInfo(operating_port_attr_.lid, qp.get()->qp_num,
                             adv_key, target_key);
      keyvalue_service_->set(adv_key, json(local_qp).dump());
      std::string result;
      keyvalue_service_->wait_for(target_key, result);
      QpInfo remote_qp = json::parse(result);
      deb(json::parse(result).dump());
      assert(remote_qp.remote_end_node_id == adv_key);

      to_rts(qp, remote_qp);

      std::cout << "all done" << std::endl;

      char *payload = static_cast<char *>(mem) + 10;
      assert_p(payload != NULL, "payload alloc");

      std::chrono::system_clock::time_point then;

      const size_t num_concurr = 1;

      std::cout << " ------------ in " << std::endl;
      // server
      // tell start
      *p = 123123123123123123;

      {
        struct ibv_sge sge = {};
        sge.lkey = mr->lkey;
        sge.addr = reinterpret_cast<uint64_t>(mem);
        sge.length = sizeof(*p);

        struct ibv_send_wr *bad_wr;
        struct ibv_send_wr this_wr = {};
        this_wr.wr_id = 1212;
        this_wr.num_sge = 1;
        this_wr.sg_list = &sge;
        this_wr.opcode = IBV_WR_SEND_WITH_IMM;
        this_wr.send_flags = IBV_SEND_SIGNALED;
        this_wr.imm_data = htonl(912999);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto bef1 = std::chrono::high_resolution_clock::now();
        int ret_post_send = ibv_post_send(qp, &this_wr, &bad_wr);
        assert_p(ret_post_send >= 0, "polling");
        deb(*p);

        struct ibv_wc completions[num_concurr];
        while (true) {
          int poll_cq_ret = ibv_poll_cq(cq, 1, completions);
          if (poll_cq_ret > 0) {
            assert_ibv_completion(completions[0].status);
            then = std::chrono::system_clock::now();
            break;
          }
        }
        auto aft1 = std::chrono::high_resolution_clock::now();
        deb(std::chrono::duration<double>(aft1 - bef1).count());
      }
    }

    void simple_recv() {
      int machine_id = 1;

      size_t amount_data_bytes_to_req = 100;  // 10GB
      size_t to_alloc = amount_data_bytes_to_req + 10;
      char *mem = static_cast<char *>(memalign(4096, to_alloc));
      size_t *p = reinterpret_cast<decltype(p)>(mem);
      int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                  IBV_ACCESS_REMOTE_WRITE;

      IbvRegMr mr(global_pd_.get(), mem, to_alloc, flags);

      std::cout << "before cq" << std::endl;

      IbvCreateCq cq(ib_context_.get(), dev_attrs_.max_cqe,
                     static_cast<void *>(NULL),
                     static_cast<struct ibv_comp_channel *>(NULL), 0);
      std::cout << "after cq" << std::endl;

      struct ibv_qp_init_attr qp_init_attr = {};
      qp_init_attr.send_cq = cq.get();
      qp_init_attr.recv_cq = cq.get();
      // qp_init_attr.cap.max_send_wr =
      // static_cast<uint33_t>(dev_attrs.max_qp_wr;
      qp_init_attr.cap.max_send_wr = 1;
      // qp_init_attr.cap.max_recv_wr =
      // static_cast<uint33_t>(dev_attrs.max_qp_wr);
      qp_init_attr.cap.max_recv_wr = 1;
      qp_init_attr.cap.max_send_sge = 1;
      qp_init_attr.cap.max_recv_sge = 1;
      // qp_init.cap.max_inline_data = 60;
      qp_init_attr.qp_type = IBV_QPT_RC;

      IbvCreateQp qp(global_pd_.get(), &qp_init_attr);

      auto target_key = std::to_string(1 - machine_id) + "exch";
      auto adv_key = std::to_string(machine_id) + "exch";

      deb(adv_key);
      deb(target_key);

      auto local_qp = QpInfo(operating_port_attr_.lid, qp.get()->qp_num,
                             adv_key, target_key);
      keyvalue_service_->set(adv_key, json(local_qp).dump());
      std::string result;
      keyvalue_service_->wait_for(target_key, result);
      QpInfo remote_qp = json::parse(result);
      deb(json::parse(result).dump());
      assert(remote_qp.remote_end_node_id == adv_key);

      to_rts(qp, remote_qp);

      std::cout << "all done" << std::endl;

      // constexpr auto cc = f(at, 2);

      {
        char *payload = static_cast<char *>(mem) + 10;
        assert_p(payload != NULL, "payload alloc");

        std::chrono::system_clock::time_point then;

        debout("start recv ###################### ");
        struct ibv_sge sge = {};
        sge.lkey = mr->lkey;
        sge.addr = reinterpret_cast<uint64_t>(mem);
        sge.length = sizeof(*p);

        struct ibv_recv_wr *bad_wr;
        struct ibv_recv_wr this_wr = {};
        this_wr.wr_id = 1212;
        this_wr.num_sge = 1;
        this_wr.sg_list = &sge;

        *p = 123;
        deb(*p);
        int ret = ibv_post_recv(qp, &this_wr, &bad_wr);

        assert_p(ret == 0, "ibv_post_recv");

        struct ibv_wc completions[1];
        while (true) {
          int ret_poll_cq = ibv_poll_cq(cq, 1, completions);
          deb(ret_poll_cq);
          assert_p(ret_poll_cq >= 0, "ibv_poll_cq");
          if (ret_poll_cq > 0) {
            assert_ibv_completion(completions[0].status);
            then = std::chrono::system_clock::now();
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        deb(*p);
      }
    }

    void init_cluster() {
      assert(keyvalue_service_->set(migrate_in_progress_cas_name_, "0"));
    }

    std::map<std::string, NodeInfo> cluster_info_;

    void prepost_do_migrate_qp(ibv_qp * qp) {
      do_migrate_sge_.lkey = do_migrate_mr_->lkey;
      do_migrate_sge_.addr = reinterpret_cast<uintptr_t>(do_migrate_mr_->addr);
      do_migrate_sge_.length = sizeof(do_migrate_req_);

      do_migrate_wr_ = ibv_recv_wr{};
      do_migrate_bad_wr_ = nullptr;
      do_migrate_wr_.wr_id = do_migrate_wrid_;
      do_migrate_wr_.num_sge = 1;
      do_migrate_wr_.sg_list = &do_migrate_sge_;
      int ret_post_recv =
          ibv_post_recv(qp, &do_migrate_wr_, &do_migrate_bad_wr_);
      assert_p(ret_post_recv == 0, "ibv_post_recv");
    }

    QpInfo find_self_qp(const std::vector<QpInfo> &infos) {
      for (const auto &it : infos) {
        if (it.remote_end_node_id == self_name_) {
          return it;
        }
      }
      assert(false);
    }

    std::vector<QpInfo> find_self_qps(
        const std::map<std::string, NodeInfo> &node_infos,
        const std::string &qp_set_key) {
      std::vector<QpInfo> ret;
      for (const auto &it : node_infos) {
        if (it.first != self_name_) {
          ret.push_back(
              find_self_qp(it.second.qp_sets_.find(qp_set_key)->second));
        }
      }
      return ret;
    }

    std::vector<uint32_t> start_migrate_ping_pong(
        const std::string &dest,
        const std::vector<slope::alloc::memory_chunk> &chunks) {
      auto migrate_dest_qp =
          fullmesh_qps_[do_migrate_qps_key_].qps_[dest].get()->get();
      auto dest_qp =
          fullmesh_qps_[shared_address_qps_key_].qps_[dest].get()->get();
      auto &cq = fullmesh_qps_[do_migrate_qps_key_].cq_;

      {
        do_migrate_req_.number_of_chunks = chunks.size();

        struct ibv_sge sge = {};
        sge.lkey = do_migrate_mr_->lkey;
        sge.addr = reinterpret_cast<uintptr_t>(&do_migrate_req_);
        sge.length = sizeof(do_migrate_req_);

        struct ibv_send_wr *bad_wr;
        struct ibv_send_wr this_wr = {};
        this_wr.wr_id = do_migrate_wrid_;
        this_wr.num_sge = 1;
        this_wr.sg_list = &sge;
        this_wr.opcode = IBV_WR_SEND_WITH_IMM;
        this_wr.send_flags = IBV_SEND_SIGNALED;
        this_wr.imm_data = htonl(self_index_);

        int ret_post_send = ibv_post_send(migrate_dest_qp, &this_wr, &bad_wr);
        assert_p(ret_post_send == 0, "ibv_post_send");
        struct ibv_wc completions[1];
        while (true) {
          int ret = ibv_poll_cq(cq.get(), 1, completions);
          if (ret > 0) {
            assert_ibv_completion(completions[0].status);
            break;
          }
        }
      }

      // {
      //   struct ibv_wc chunks_completions[1];
      //   while(true) {
      //     int chunks_ret_poll_cq = ibv_poll_cq(
      //         do_migrate_cq_, 1, chunks_completions);
      //     assert_p(chunks_ret_poll_cq >= 0 && chunks_ret_poll_cq <= 1,
      //         "ibv_poll_cq");
      //     if(chunks_ret_poll_cq > 0) {
      //       assert_p(chunks_completions[0].status == 0, "ibv_poll_cq");
      //       break;
      //     }
      //   }
      // }

      std::vector<DoMigrateChunk> chunks_info(chunks.size());
      auto chunks_info_sz = chunks.size() * sizeof(DoMigrateChunk);
      for (size_t i = 0; i < chunks.size(); i++) {
        chunks_info[i].addr = chunks[i].first;
        chunks_info[i].sz = chunks[i].second;
      }
      {
        IbvRegMr mr(global_pd_.get(), chunks_info.data(), chunks_info_sz,
                    do_migrate_mr_flags_);
        deb(chunks_info_sz);
        deb(chunks_info.size());

        struct ibv_sge sge = {};
        sge.lkey = mr.get()->lkey;
        sge.addr = reinterpret_cast<uintptr_t>(chunks_info.data());
        sge.length = chunks_info_sz;

        struct ibv_send_wr *bad_wr;
        struct ibv_send_wr this_wr = {};
        this_wr.wr_id = chunks_info_wrid_;
        this_wr.num_sge = 1;
        this_wr.sg_list = &sge;
        this_wr.opcode = IBV_WR_SEND_WITH_IMM;
        this_wr.send_flags = IBV_SEND_SIGNALED;
        this_wr.imm_data = htonl(12345);

        int ret_post_send = ibv_post_send(dest_qp, &this_wr, &bad_wr);
        assert_p(ret_post_send == 0, "ibv_post_send");
        struct ibv_wc completions[1];

        while (true) {
          int ret = ibv_poll_cq(cq.get(), 1, completions);
          if (ret > 0) {
            assert_ibv_completion(completions[0].status);
            assert(completions[0].wr_id == chunks_info_wrid_);
            break;
          }
        }
      }

      auto pages = slope::alloc::chunks_to_pages(chunks);
      auto remote_rkeys = recv_vector<uint32_t>(
          pages.size(), destination_rkeys_wrid_, dest_qp, cq.get());

      // {
      //   struct ibv_recv_wr *in_bad_wr;
      //   struct ibv_recv_wr in_this_wr = {};
      //   in_this_wr.wr_id = received_chunks_wrid_;
      //   in_this_wr.num_sge = 0;
      //   in_this_wr.sg_list = NULL;

      //   int ret = ibv_post_recv(dest_qp, &in_this_wr, &in_bad_wr);
      //   assert_p(ret == 0, "ibv_post_recv");
      // }

      // {
      //   struct ibv_wc completions[1];
      //   while (true) {
      //     int chunks_ret_poll_cq = ibv_poll_cq(do_migrate_cq_, 1,
      //     completions); assert_p(chunks_ret_poll_cq >= 0 &&
      //     chunks_ret_poll_cq <= 1,
      //              "ibv_poll_cq");
      //     if (chunks_ret_poll_cq > 0) {
      //       assert_p(completions[0].status == 0, "ibv_poll_cq");
      //       deb(completions[0].wr_id);
      //       deb(received_chunks_wrid_);
      //       assert(completions[0].wr_id == received_chunks_wrid_);
      //       break;
      //     }
      //   }
      // }

      // {
      //   std::stringstream deb_ss;
      //   deb_ss << std::showbase << std::internal << std::setfill('0')
      //     << "addr: " << std::hex << std::setw(16) <<
      //     static_cast<void*>(chunks_info.data());
      //   infoout(deb_ss.str());
      // }
      return remote_rkeys;
    }

    void transfer_ownership_ping_pong(
        const std::string &dest,
        const std::vector<slope::alloc::memory_chunk> &chunks) {}

    static inline const std::string migrate_in_progress_cas_name_ =
        "MIGRATE_IN_PROGRESS_CAS";
    static inline const std::string do_migrate_qps_key_ = "do_migrate_qps";
    static inline const std::string shared_address_qps_key_ =
        "shared_address_qps";
    static inline const std::string time_calib_qps_key_ = "time_calib_qps";
    static inline const int time_calib_rounds_ = 10;
    static inline const uint64_t do_migrate_wrid_ = 0xd017;
    static inline const uint64_t calibrate_time_wrid_ = 0xd018;
    static inline const uint64_t chunks_info_wrid_ = 0xd019;
    static inline const uint64_t received_chunks_wrid_ = 0xd01a;
    static inline const uint64_t destination_rkeys_wrid_ = 0xd01b;
    static inline const uint64_t do_prefill_wrid_ = 0xd01c;

    std::string peer_done_key(const std::string &peer_name) {
      return peer_name + "_DONE";
    }

    // ************ order is important here *********************
    static inline const std::string ib_device_name_ = "mlx5_1";
    slope::keyvalue::KeyValueService::ptr keyvalue_service_;
    slope::data::DataPlane::ptr dataplane_;

    std::mutex control_plane_polling_lock_;

    IbvDeviceContextByName ib_context_;
    int dev_attrs_result_;
    ibv_device_attr dev_attrs_;
    static inline const uint16_t operating_pkey_ = 0;
    int query_port_result_;
    static inline const uint8_t operating_port_num_ = 1;
    struct ibv_port_attr operating_port_attr_;
    IbvAllocPd global_pd_;
    static inline const int do_migrate_mr_flags_ = IBV_ACCESS_LOCAL_WRITE |
                                                   IBV_ACCESS_REMOTE_READ |
                                                   IBV_ACCESS_REMOTE_WRITE;
    static inline const int shared_address_mr_flags_ = IBV_ACCESS_LOCAL_WRITE |
                                                       IBV_ACCESS_REMOTE_READ |
                                                       IBV_ACCESS_REMOTE_WRITE;
    static inline const int final_read_mr_flags_ = IBV_ACCESS_REMOTE_READ;
    static inline const int final_confirmation_value_ = 132;
    static inline const int sender_prefill_mr_flags_ = 0;
    DoMigrateRequest do_migrate_req_;
    IbvRegMr do_migrate_mr_;
    IbvCreateCq do_migrate_cq_;
    IbvCreateCq time_calib_cq_;
    // TODO: This is incorrect for more than 1 peer. Must have (wr,sge) set per
    // peer.
    ibv_recv_wr do_migrate_wr_;
    ibv_recv_wr *do_migrate_bad_wr_;
    ibv_sge do_migrate_sge_;

    std::map<std::string, FullMeshQpSet> fullmesh_qps_;
    // **********************************************************

    // std::vector<std::shared_ptr<
  };

}  // namespace control
}  // namespace slope

#endif  // SLOPE_RDMA_CONTROL_H_
