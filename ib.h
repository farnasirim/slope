#ifndef IB_H_
#define IB_H_
#include <memory>
#include <functional>
#include <type_traits>

#include <infiniband/verbs.h>

#include "fnp_traits.h"
#include "fnp_types.h"
#include "ib_utils.h"

template<typename Factory, Factory f, typename Deleter, Deleter d,
  typename = typename FnpTypes<decltype(f)>::PackedArgs>
class IbResource;

template<typename Factory, Factory f, typename Deleter, Deleter d, typename... Args>
class IbResource <Factory, f, Deleter, d, Pack<Args...>> {
 public:
  using ResourceType = std::remove_pointer_t<typename FnpTypes<decltype(f)>::ReturnType>;

  std::unique_ptr<ResourceType, VoidDeleter<ResourceType>> ptr_;

  template<typename... FuncArgs,
    typename = std::enable_if_t<
      IsSameNoCVR<
          std::tuple<Args...>, std::tuple<FuncArgs...>
        >::value
      >
  >
  IbResource(FuncArgs&&... args): ptr_(
      factory_wrapper(f, FnpTraits<Factory, f>::name())(
        // TODO: explicitly static cast the FuncArgs to Args
        std::forward<FuncArgs>(args)...),
      int_deleter_wrapper(d, FnpTraits<Deleter, d>::name())) {
  }

  operator ResourceType*() const {
    return ptr_.get();
  }

  ResourceType *operator->() {
    return ptr_.get();
  }

  ResourceType *get() const {
    return ptr_.get();
  }
};

struct ibv_context *ibv_device_context_by_name_(const char *name);

using IbvDeviceContextByName = IbResource<
  decltype(&ibv_device_context_by_name_), &ibv_device_context_by_name_,
  decltype(&ibv_close_device), &ibv_close_device
  >;

using IbvAllocPd = IbResource<
  decltype(&ibv_alloc_pd), &ibv_alloc_pd,
  decltype(&ibv_dealloc_pd), &ibv_dealloc_pd
  >;

using IbvRegMr = IbResource<
  decltype(&ibv_reg_mr), &ibv_reg_mr,
  decltype(&ibv_dereg_mr), &ibv_dereg_mr
  >;

using IbvCreateCq = IbResource<
  decltype(&ibv_create_cq), &ibv_create_cq,
  decltype(&ibv_destroy_cq), &ibv_destroy_cq
  >;

using IbvCreateQp = IbResource<
  decltype(&ibv_create_qp), &ibv_create_qp,
  decltype(&ibv_destroy_qp), &ibv_destroy_qp
  >;

#endif  // IB_H_
