#ifndef SLOPE_TESTDRIVE_MIGRATE_H_
#define SLOPE_TESTDRIVE_MIGRATE_H_

#include "discovery.h"
#include "json.hpp"

#include "control.h"

using json = nlohmann::json;

void testdrive_migrate(slope::discovery::DiscoverySerivce& d,
    slope::control::ControlPlane::ptr control_plane,
    const char *node_id);

void do_src(slope::discovery::DiscoverySerivce& d,
    slope::control::ControlPlane::ptr control_plane,
    const char *node_id);

void do_snk(slope::discovery::DiscoverySerivce& d,
    slope::control::ControlPlane::ptr control_plane,
    const char *node_id);

void do_common(slope::discovery::DiscoverySerivce& d,
    slope::control::ControlPlane::ptr control_plane,
    const char *node_id);


struct NodeInfo {
  std::string node_id;
};

void to_json(json& j, const NodeInfo& inf);

void from_json(const json& j, NodeInfo& inf);

#endif  // SLOPE_TESTDRIVE_MIGRATE_H_
