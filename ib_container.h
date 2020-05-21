#pragma once

#include <type_traits>

#include "fnp_traits.h"
#include "fnp_types.h"

#include "debug.h"

#include <infiniband/verbs.h>

template<typename Factory, Factory f,
  typename = typename FnpTypes<decltype(f)>::PackedArgs>
class IbObjectContainer;

template<typename Factory, Factory f, typename... Args>
class IbObjectContainer<Factory, f, Pack<Args...>> {
 public:
  using ResourceType = std::remove_pointer_t<
    typename FnpTypes<decltype(f)>::LastArg
    >;

  template<typename... FuncArgs,
    typename = std::enable_if_t<
      IsSameNoCVR<
          std::tuple<Args...>, std::tuple<FuncArgs..., ResourceType *>
        >::value
      >
  >
  IbObjectContainer(FuncArgs&&... args):
    obj{},
    result(f(std::forward<FuncArgs...>(args)..., &obj)) { }
  // result is constructed after obj (see the order in the private section),
  // Making it safe to use &obj in calling the function. Also obj is default
  // constructed, setting all field to zero.

 std::add_lvalue_reference_t<std::add_const_t<ResourceType>> get() const {
    return obj;
  }
 private:
  ResourceType obj;
  int result = -1;
};

using IbQueryPort = IbObjectContainer<
  decltype(&ibv_query_port), &ibv_query_port
  >;

using IbQueryDevice = IbObjectContainer<
  decltype(&ibv_query_device), &ibv_query_device
  >;
