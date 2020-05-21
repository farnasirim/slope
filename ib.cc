#include "ib.h"

#include <cstdlib>
#include <cassert>
#include <cstdio>

#include <iostream>

struct ibv_context *ibv_device_context_by_name_(const char *name) {
  struct ibv_device **ibv_dev = ibv_get_device_list(NULL);
  struct ibv_device **current_device = ibv_dev;

  while(*current_device) {
    if(!strcmp((*current_device)->name, name)) {
      struct ibv_context *maybe_device_ctx = ibv_open_device(*current_device);
      ibv_free_device_list(ibv_dev);
      if(maybe_device_ctx == NULL) {
        perror("ibv_open_device");
        std::abort();
      }
      return maybe_device_ctx;
    }
    current_device ++;
  }
  ibv_free_device_list(ibv_dev);
  assert(false);
  return NULL;
}
