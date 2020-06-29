#include "rdma_control.h"

#include <cassert>
#include <arpa/inet.h>
#include <thread>
#include <cstdlib>
#include <malloc.h>
#include <chrono>

#include "mig.h"
#include "data.h"
#include "discovery.h"

#include "json.hpp"

#include "debug.h"

namespace slope {
namespace control {

void RdmaControlPlane::simple_send() {
  int machine_id = 0;

  size_t amount_data_bytes_to_req = 100; // 10GB
  size_t to_alloc = amount_data_bytes_to_req + 10;
  char *mem = static_cast<char *>(memalign(4096, to_alloc));
  size_t *p = reinterpret_cast<decltype(p)>(mem);
  int flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  IbvRegMr mr(global_pd_.get(), mem, to_alloc, flags);

  std::cout << "before cq" << std::endl;

  IbvCreateCq cq(ib_context_.get(), dev_attrs_.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0);
  std::cout << "after cq" << std::endl;

  struct ibv_qp_init_attr qp_init_attr = {};
  qp_init_attr.send_cq = cq.get();
  qp_init_attr.recv_cq = cq.get();
  // qp_init_attr.cap.max_send_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr;
  qp_init_attr.cap.max_send_wr = 1;
  // qp_init_attr.cap.max_recv_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr);
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

  auto local_qp = QpInfo(operating_port_attr_.lid, qp.get()->qp_num, adv_key, target_key);
  keyvalue_service_->set(adv_key, json(local_qp).dump());
  std::string result;
  keyvalue_service_->wait_for(target_key , result);
  QpInfo remote_qp = json::parse(result);
  deb(json::parse(result).dump());
  assert(remote_qp.remote_end_node_id == adv_key);

  to_rts(qp, remote_qp);

  std::cout << "all done" << std::endl;

  char *payload = static_cast<char *>(mem) + 10;
  assert_p(payload != NULL, "payload alloc");

  std::chrono::system_clock::time_point then;


    size_t msg_size_to_req = 100;
    size_t num_messages_to_request = amount_data_bytes_to_req / msg_size_to_req;
    const size_t num_concurr = 1;

    std::cout << " ------------ in " << std::endl;
    // server
    // tell start
    *p = 123123123123123123;

    {
    struct ibv_sge sge = {};
    sge.lkey = mr->lkey;
    sge.addr = reinterpret_cast<uint64_t>(mem);
    sge.length = sizeof (*p);


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
    while(true) {
      int poll_cq_ret = ibv_poll_cq(cq, 1, completions);
      if(poll_cq_ret > 0) {
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        then = std::chrono::system_clock::now();
        break;
      }
    }
    auto aft1 = std::chrono::high_resolution_clock::now();
    deb(std::chrono::duration<double>(aft1 - bef1).count());
    }
}



void RdmaControlPlane::simple_recv() {
  int machine_id = 1;

  size_t amount_data_bytes_to_req = 100; // 10GB
  size_t to_alloc = amount_data_bytes_to_req + 10;
  char *mem = static_cast<char *>(memalign(4096, to_alloc));
  size_t *p = reinterpret_cast<decltype(p)>(mem);
  int flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  IbvRegMr mr(global_pd_.get(), mem, to_alloc, flags);

  std::cout << "before cq" << std::endl;

  IbvCreateCq cq(ib_context_.get(), dev_attrs_.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0);
  std::cout << "after cq" << std::endl;

  struct ibv_qp_init_attr qp_init_attr = {};
  qp_init_attr.send_cq = cq.get();
  qp_init_attr.recv_cq = cq.get();
  // qp_init_attr.cap.max_send_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr;
  qp_init_attr.cap.max_send_wr = 1;
  // qp_init_attr.cap.max_recv_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr);
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

  auto local_qp = QpInfo(operating_port_attr_.lid, qp.get()->qp_num, adv_key, target_key);
  keyvalue_service_->set(adv_key, json(local_qp).dump());
  std::string result;
  keyvalue_service_->wait_for(target_key , result);
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
  sge.length = sizeof (*p);

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
  while(true) {
    int ret_poll_cq = ibv_poll_cq(cq, 1, completions);
    deb(ret_poll_cq);
    assert_p(ret_poll_cq >= 0, "ibv_poll_cq");
    if(ret_poll_cq > 0) {
      assert_p(completions[0].status == 0, "ibv_poll_cq");
      then = std::chrono::system_clock::now();
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  deb(*p);
  auto last_addr = payload;
  }
}

}  // namespace control
}  // namespace slope
