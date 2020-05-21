#ifndef MODIFY_QP_H_
#define MODIFY_QP_H_

#include <infiniband/verbs.h>
#include <type_traits>

#include "fnp_types.h"
//   static constexpr int flag = T::flag;
//   inline ibv_qp_attr& do(ibv_qp_attr& attrs, X value) {
//
//   }
// };


#include <iostream>
#include <fstream>

#include "debug.h"
#include <cmath>
#include <algorithm>

namespace qp_attr {

using flag_type = ibv_qp_attr_mask;

template<typename AttrName>
struct QpAttr {
  // static constexpr typename AttrName::type FlagValue = AttrName::value;
  inline void execute(struct ibv_qp_attr& attr) const {
    static_cast<const AttrName *>(this)->execute(attr);
  }

  inline int get() const {
    return static_cast<const AttrName *>(this)->get();
  }
  virtual ~QpAttr() = default;
};
template<typename... Args>
struct BitwiseOr;

template<typename... Args>
struct BitwiseOr<Pack<Args...>> {
  static constexpr decltype(Pack<Args...>::first::flag_value) value =
    static_cast<decltype(Pack<Args...>::first::flag_value)>(
        Pack<Args...>::first::flag_value | BitwiseOr<typename Pack<Args...>::rest>::value
        );
};
template<typename T>
struct BitwiseOr<Pack<T>> {
  static constexpr decltype(T::flag_value) value = T::flag_value;
};

inline int modify_qp_impl(ibv_qp *qp, struct ibv_qp_attr& attr, flag_type& flags) {
  return ibv_modify_qp(qp, &attr, flags);
}

template<typename T, typename... Args>
int modify_qp_impl(ibv_qp *qp, struct ibv_qp_attr& attr, flag_type flags,
    const QpAttr<T>& next_opt, const QpAttr<Args>&... opts) {
  next_opt.execute(attr);
  return modify_qp_impl(qp, attr, flags, opts...);
}

#define ib_generate_attr_template_(field_name, flag_name) \
class field_name: public QpAttr<field_name> { \
 public: \
  using type = decltype(ibv_qp_attr::field_name); \
  static constexpr flag_type flag_value = flag_name; \
  field_name(type field): field_(field) { } \
  inline type get() const {\
    return field_; \
  }\
  inline void execute(ibv_qp_attr& attr) const { \
    attr.field_name = field_; \
  } \
 private: \
  const type field_; \
}


ib_generate_attr_template_(qp_state, IBV_QP_STATE);
ib_generate_attr_template_(cur_qp_state, IBV_QP_CUR_STATE);
ib_generate_attr_template_(en_sqd_async_notify, IBV_QP_EN_SQD_ASYNC_NOTIFY);
ib_generate_attr_template_(qp_access_flags, IBV_QP_ACCESS_FLAGS);
ib_generate_attr_template_(pkey_index, IBV_QP_PKEY_INDEX);
ib_generate_attr_template_(port_num, IBV_QP_PORT);
ib_generate_attr_template_(qkey, IBV_QP_QKEY);
ib_generate_attr_template_(ah_attr, IBV_QP_AV);
ib_generate_attr_template_(path_mtu, IBV_QP_PATH_MTU);
ib_generate_attr_template_(timeout, IBV_QP_TIMEOUT);
ib_generate_attr_template_(retry_cnt, IBV_QP_RETRY_CNT);
ib_generate_attr_template_(rnr_retry, IBV_QP_RNR_RETRY);
ib_generate_attr_template_(rq_psn, IBV_QP_RQ_PSN);
ib_generate_attr_template_(max_rd_atomic, IBV_QP_MAX_QP_RD_ATOMIC);
ib_generate_attr_template_(alt_timeout, IBV_QP_ALT_PATH);
ib_generate_attr_template_(min_rnr_timer, IBV_QP_MIN_RNR_TIMER);
ib_generate_attr_template_(sq_psn, IBV_QP_SQ_PSN);
ib_generate_attr_template_(max_dest_rd_atomic, IBV_QP_MAX_DEST_RD_ATOMIC);
ib_generate_attr_template_(path_mig_state, IBV_QP_PATH_MIG_STATE);
ib_generate_attr_template_(cap, IBV_QP_CAP);
ib_generate_attr_template_(dest_qp_num, IBV_QP_DEST_QPN);



template<typename... Args>
int modify_qp(ibv_qp *qp, const QpAttr<Args>&... opts) {
  ibv_qp_attr attr = {};
  flag_type flags = BitwiseOr<Pack<Args...>>::value;
  return modify_qp_impl(qp, attr, flags, opts...);
}

#undef ib_generate_attr_template_
}

#endif  // MODIFY_QP_H_

