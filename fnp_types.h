#ifndef FNP_TYPES_H_
#define  FNP_TYPES_H_

template<typename... Args>
struct Pack;

template<typename T, typename... Args>
struct Pack<T, Args...> {
  using first = T;
  using rest = Pack<Args...>;
};

template<typename T>
struct Pack<T> {
  using first = T;
  using rest = Pack<>;
};

template<typename FactoryType>
struct FnpTypes;

template<typename T>
struct LastArg;

template<typename T, typename... Args>
struct LastArg<Pack<T, Args...>> {
  using type = typename LastArg<Pack<Args...>>::type;
};

template<typename T>
struct LastArg<Pack<T>> {
  using type = T;
};

template <typename Res, typename... Args>
struct FnpTypes<Res (*)(Args...)> {
 public:
  using ReturnType = Res;
  using PackedArgs = Pack<Args...>;
  using LastArg = typename LastArg<Pack<Args...>>::type;
};

#endif  // FNP_TYPES_H_
