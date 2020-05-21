#include <cstdlib>
#include <cmath>
#include <thread>
#include <cassert>
#include <cstring>
#include <cstdio>

#include <errno.h>
#include <malloc.h>

#include <iostream>
#include <algorithm>
#include <vector>

#include <libmemcached/memcached.h>

#include "discovery.h"
#include "ib.h"
#include "ib_container.h"
#include "logging.h"
#include "modify_qp.h"

#include "debug.h"

#include "testdrive_migrate.h"

struct payload {
  int code;
  char msg[20];
};

int main(int argc, char **argv) {
  if(argc != 3) {
    std::cerr << "incorrect num args" << std::endl;
    exit(-1);
  }
  int machine_id = atoi(argv[1]);
  char *memcached_confstr = argv[2];

  testdrive_migrate(machine_id);
  IbvDeviceContextByName ib_context("mlx5_1");
  IbvAllocPd pd(ib_context.get());

  // size_t num_messages = 128;
  // size_t msg_size = 256;
  // size_t mem_size = msg_size * num_messages;
  size_t amount_data_bytes_to_req = 100; // 10GB
  size_t to_alloc = amount_data_bytes_to_req + 10;
  char *mem = static_cast<char *>(memalign(4096, to_alloc));
  int *p = reinterpret_cast<decltype(p)>(mem);
  int flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  IbvRegMr mr(pd.get(), mem, to_alloc, flags);

  ibv_device_attr dev_attrs = {};
  {
    int ret = ibv_query_device(ib_context.get(), &dev_attrs);
    deb(static_cast<int>(dev_attrs.phys_port_cnt));
    assert(!ret);
  }

  struct ibv_port_attr port_attr = {};
  {
    int ret = ibv_query_port(ib_context.get(), 1, &port_attr);
    deb(port_attr.lid);
    assert(!ret);
  }

  std::cout << "before cq" << std::endl;

  IbvCreateCq cq(ib_context.get(), dev_attrs.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0);
  std::cout << "after cq" << std::endl;

  {
    int ret = ibv_query_port(ib_context.get(), 1, &port_attr);
    deb(port_attr.lid);
    assert(!ret);
  }


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

  IbvCreateQp qp(pd.get(), &qp_init_attr);

  {
    int ret = ibv_query_port(ib_context.get(), 1, &port_attr);
    deb(port_attr.lid);
    assert(!ret);
  }


  memcached_st *memc = memcached(memcached_confstr, strlen(memcached_confstr));
  assert_p(memc != NULL, "memc");

  auto target_key = std::to_string(1 - machine_id);
  auto adv_key = std::to_string(machine_id);

  deb(adv_key);
  deb(target_key);

  auto local_qp = make_qp_info(port_attr.lid, qp.get()->qp_num);
  // auto remote_qp = exchange_qp_info(local_qp);
  auto remote_qp = exchange_qp_info(memc, adv_key.c_str(), target_key.c_str(), local_qp);

  deb(local_qp.qp_num);
  deb(local_qp.lid);
  std::cout << std::endl;
  deb(remote_qp.qp_num);
  deb(remote_qp.lid);
  std::cout << std::endl;

  {
    int ret = qp_attr::modify_qp(qp,
     qp_attr::qp_state(IBV_QPS_INIT),
     qp_attr::pkey_index(0),
     qp_attr::port_num(1),
     qp_attr::qp_access_flags(IBV_ACCESS_REMOTE_READ
       | IBV_ACCESS_REMOTE_WRITE
       | IBV_ACCESS_REMOTE_ATOMIC)
     );

    perror("init");
    assert_p(ret == 0, "init");
  }

  {
    struct ibv_ah_attr ah_attrs = {};
    ah_attrs.is_global = 0;
    ah_attrs.dlid = remote_qp.lid;
    ah_attrs.sl = 0;
    ah_attrs.src_path_bits = 0;
    ah_attrs.port_num = 1;

    int ret = modify_qp(qp,
        qp_attr::qp_state(IBV_QPS_RTR),
        qp_attr::path_mtu(IBV_MTU_4096),
        qp_attr::dest_qp_num(remote_qp.qp_num),
        qp_attr::rq_psn(0),
        qp_attr::max_dest_rd_atomic(16),
        qp_attr::min_rnr_timer(12),
        qp_attr::ah_attr(ah_attrs)
        );

    assert_p(ret == 0, "rtr");
  }

  {
    std::cout << "doing rts" << std::endl;

    int ret = modify_qp(qp,
        qp_attr::qp_state(IBV_QPS_RTS),
        qp_attr::max_rd_atomic(1),
        qp_attr::retry_cnt(7),
        qp_attr::rnr_retry(7),
        qp_attr::sq_psn(0),
        qp_attr::timeout(0x12),
        qp_attr::max_rd_atomic(1)
      );

    assert_p(ret == 0, "rts");
  }

  std::cout << "all done" << std::endl;

  IbQueryDevice qd(ib_context.get());
  // std::cout << qd.get().max_qp << std::endl;

  // constexpr auto cc = f(at, 2);

  char *payload = static_cast<char *>(mem) + 10;
  assert_p(payload != NULL, "payload alloc");

  std::chrono::system_clock::time_point then;


  if(machine_id == 0) {
    size_t msg_size_to_req = 100;
    size_t num_messages_to_request = amount_data_bytes_to_req / msg_size_to_req;
    const size_t num_concurr = 1;

    std::cout << " ------------ in server" << std::endl;
    // server
    // tell start
    *p = msg_size_to_req;

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
    int ret_post_send = ibv_post_send(qp, &this_wr, &bad_wr);
    assert_p(ret_post_send >= 0, "polling");
    deb(*p);
    }

    struct ibv_wc completions[num_concurr];
    while(true) {
      int ret = ibv_poll_cq(cq, 1, completions);
      if(ret > 0) {
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        then = std::chrono::system_clock::now();
        break;
      }
    }
    std::cout << "signaled" << std::endl;

    size_t current_posted = 0;
    char *last_addr = payload;
    {
    int ret = ibv_poll_cq(cq, num_concurr, completions);
    deb(ret);
    }
    deb(num_messages_to_request);
    int done = 0;
    //num_messages_to_request --;
    while(num_messages_to_request) {
      // std::cout << "here" << std::endl;
      int ret = ibv_poll_cq(cq, num_concurr, completions);
      if(ret > 0) {
        std::cout << "poll suc" << std::endl;
        for(int i = 0; i < ret; i++) {
          assert_p(completions[i].status == 0, "ibv_poll_cq");
        }
      }
      assert_p(ret >= 0, "ret < 0 incorrectly");
      num_messages_to_request -= static_cast<size_t>(ret);
      current_posted -= static_cast<size_t>(ret);
      done += ret;
      while (current_posted < num_concurr && num_messages_to_request > current_posted) {
        current_posted += 1;
        struct ibv_sge sge = {};
        sge.lkey = mr->lkey;
        sge.addr = reinterpret_cast<uint64_t>(last_addr);
        sge.length = msg_size_to_req;

        last_addr += msg_size_to_req;

        struct ibv_recv_wr *bad_wr;
        struct ibv_recv_wr this_wr = {};
        this_wr.wr_id = 1212;
        this_wr.num_sge = 1;
        this_wr.sg_list = &sge;

        int ret_post_recv = ibv_post_recv(qp, &this_wr, &bad_wr);
        assert_p(ret_post_recv == 0, "ibv_post_recv");
      }
    }
    deb(done);


    for(long long i = 0; i < static_cast<long long>(to_alloc); i++) {
      std::cout << static_cast<int>(mem[i]) << " ";
    }
    std::cout << std::endl;

  } else {
    struct ibv_sge sge = {};
    sge.lkey = mr->lkey;
    sge.addr = reinterpret_cast<uint64_t>(mem);
    sge.length = sizeof (*p);

    struct ibv_recv_wr *bad_wr;
    struct ibv_recv_wr this_wr = {};
    this_wr.wr_id = 1212;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;

    int ret = ibv_post_recv(qp, &this_wr, &bad_wr);

    assert_p(ret == 0, "ibv_post_recv");

    struct ibv_wc completions[1];
    while(true) {
      int ret_poll_cq = ibv_poll_cq(cq, 1, completions);
      assert_p(ret_poll_cq >= 0, "ibv_poll_cq");
      if(ret_poll_cq > 0) {
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        then = std::chrono::system_clock::now();
        break;
      }
    }
    deb(amount_data_bytes_to_req);
    deb(*p);
    deb(amount_data_bytes_to_req/static_cast<size_t>(*p));
    auto last_addr = payload;

    deb(last_addr - reinterpret_cast<char *>(p));

    size_t nums = amount_data_bytes_to_req/static_cast<size_t>(*p);
    //nums --;
    size_t i;

    for(long long j = 10; j < static_cast<long long>(to_alloc); j++) {
      mem[j] = j % 256;
    }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for(i = 0; i < nums; i++) {

      struct ibv_sge final_sge = {};
      final_sge.lkey = mr->lkey;
      final_sge.addr = reinterpret_cast<uint64_t>(last_addr);
      // deb(final_sge.addr);
      // deb(reinterpret_cast<uint64_t>(mem));
      // deb(reinterpret_cast<uint64_t>((char *) mem + amount_data_bytes_to_req + 10) - final_sge.addr);
      last_addr += *p;
      deb(*p);
      deb(static_cast<long long>(*(p)) - 1);
      final_sge.length = static_cast<uint32_t>(*p);
      // deb(*p);
      //
      /*
      // */

      struct ibv_send_wr *final_bad_wr;
      struct ibv_send_wr other_wr = {};
      other_wr.wr_id = 1212;
      other_wr.num_sge = 1;
      other_wr.sg_list = &final_sge;
      other_wr.opcode = IBV_WR_SEND_WITH_IMM;
      other_wr.send_flags = IBV_SEND_SIGNALED;
      other_wr.imm_data = htonl(912999);

      int ret_post_send_final = ibv_post_send(qp, &other_wr, &final_bad_wr);
      struct ibv_wc comp;
      int r;
      while((r = ibv_poll_cq(cq, 1, &comp)) == 0) {  }
      assert_p(r > 0, "poll");

      assert_p(ret_post_send_final == 0, "send");
    }
    deb(i);
    for(long long j = 0; j < static_cast<long long>(to_alloc); j++) {
      std::cout << static_cast<int>(mem[j]) << " ";
    }
    std::cout << std::endl;
  }

  auto x = std::chrono::system_clock::now() - then;
  auto mics = std::chrono::nanoseconds(x).count()/1000.0;
  auto dpus = amount_data_bytes_to_req/mics;
  auto mpus = (amount_data_bytes_to_req / static_cast<size_t>(*p)) / mics;

  deb(mics/1e6);
  deb(dpus);
  deb(mpus);

  std::this_thread::sleep_for(std::chrono::seconds(1));

  memcached_free(memc);
  return 0;
}
