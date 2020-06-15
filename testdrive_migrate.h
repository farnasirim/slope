#ifndef SLOPE_TESTDRIVE_MIGRATE_H_
#define SLOPE_TESTDRIVE_MIGRATE_H_

#include "json.hpp"

#include "control.h"
#include "rdma_control.h"

using json = nlohmann::json;

void testdrive_migrate(slope::control::RdmaControlPlane::ptr control_plane);

#endif  // SLOPE_TESTDRIVE_MIGRATE_H_
