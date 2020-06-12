#include "rdma_control.h"

#include <cassert>

#include "mig.h"

namespace slope {
namespace control {

RdmaControlPlane::RdmaControlPlane(std::string self_name,
      std::vector<std::string> cluster_nodes,
      std::string kv_prefix,
      slope::keyvalue::KeyValueService::ptr keyvalue_service,
      slope::dataplane::DataPlane::ptr dataplane):
  self_name_(std::move(self_name)),
  cluster_nodes_(std::move(cluster_nodes)),
  keyvalue_service_(std::make_shared<slope::keyvalue::KeyValuePrefixMiddleware>(
        keyvalue_service,
        kv_prefix) ),
  dataplane_(dataplane) {

}

bool RdmaControlPlane::do_migrate(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks) {
  if(!keyvalue_service_->compare_and_swap(
      migrate_in_progress_cas_name_,
      "0",
      "1")) {
    return false;
  }

  start_migrate_ping_pong(dest, chunks);
  // Later TODO: prefill

  for(auto& chunk: chunks) {
    auto mprotect_result = mprotect(
        reinterpret_cast<void*>(chunk.first),
        chunk.second, PROT_READ);
    assert(mprotect_result);
  }

  transfer_ownership_ping_pong(dest, chunks);

  // TODO: broadcast ownership changes

  assert(keyvalue_service_->compare_and_swap(
      migrate_in_progress_cas_name_,
      "1",
      "0"));
  return true;
}

void RdmaControlPlane::start_migrate_ping_pong(const std::string& dest,
    const std::vector<slope::alloc::memory_chunk>& chunks) {

}

void RdmaControlPlane::transfer_ownership_ping_pong(const std::string& dest,
    const std::vector<slope::alloc::memory_chunk>& chunks) {

}

bool RdmaControlPlane::init_kvservice() {
  return keyvalue_service_->set(migrate_in_progress_cas_name_,
      "0");
}

}  // namespace control
}  // namespace slope
