#ifndef SLOPE_TESTDRIVE_MIGRATE_H_
#define SLOPE_TESTDRIVE_MIGRATE_H_

#include "discovery.h"

void testdrive_migrate(slope::discovery::DiscoverySerivce& d, const char *node_id);

void do_src(slope::discovery::DiscoverySerivce& d, const char *node_id);


void do_snk(slope::discovery::DiscoverySerivce& d, const char *node_id);

void do_common(slope::discovery::DiscoverySerivce& d, const char *node_id);

#endif  // SLOPE_TESTDRIVE_MIGRATE_H_
