#ifndef SLOPE_TESTDRIVE_MIGRATE_H_
#define SLOPE_TESTDRIVE_MIGRATE_H_

#include "json.hpp"

#include "control.h"
#include "rdma_control.h"
#include "allocator.h"

using json = nlohmann::json;

template<typename T>
using mig_alloc = slope::alloc::FixedPoolAllocator<T>;
template<typename T>
using mig_vector = std::vector<T, mig_alloc<T>>;
using td_mig_type = mig_vector<int>;

void testdrive_migrate(typename
    slope::control::RdmaControlPlane<td_mig_type>::ptr control_plane);

#endif  // SLOPE_TESTDRIVE_MIGRATE_H_
