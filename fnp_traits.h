#ifndef FNP_TRAITS_H_
#define FNP_TRAITS_H_

#include <typeinfo>
#include <algorithm>
#include <string>

template<typename T, T f>
struct FnpTraits {
  static std::string name() {
    std::string pretty_name = __PRETTY_FUNCTION__;

    std::string start_marker = "f = ", maybe_end_chars= ";,]";

    auto start_index = pretty_name.find(start_marker) + start_marker.size();
    auto end_index = pretty_name.find_first_of(maybe_end_chars, start_index);

    return pretty_name.substr(start_index, end_index - start_index);
  }
};

template<typename T1, typename T2>
struct IsSameNoCVR;

template<typename T1, typename... A1, typename T2, typename... A2>
struct IsSameNoCVR <std::tuple<T1, A1...>, std::tuple<T2, A2...>> {
 public:
  static constexpr bool value = true;
  // static constexpr bool value = std::is_same<
  //       std::remove_reference_t<T1>,
  //       std::remove_reference_t<T2>>::value &&
  //       IsSameNoCVR<std::tuple<A1...>, std::tuple<A2...> >::value;
};

template<>
struct IsSameNoCVR <std::tuple<>, std::tuple<>> {
 public:
  static constexpr bool value = true;
};

template<typename T>
struct IsCharStar {
  static constexpr bool value = std::is_same<char *,
                   std::remove_reference_t<std::remove_cv_t<std::remove_reference_t<T>>>>::value;
};

template<typename T>
struct IsCharArray {
  static constexpr bool value = std::is_array<T>::value && std::is_same<char,
                   std::remove_extent_t<std::remove_reference_t<std::remove_cv_t<std::remove_reference_t<T>>>>>::value;
};

template<typename T1, typename T2,
  typename = std::enable_if_t<(IsCharArray<T1>::value && IsCharStar<T2>::value) ||
  (IsCharArray<T2>::value && IsCharStar<T1>::value)>
 >
struct IsCharPair {
 public:
  static constexpr bool value = true;
};



#endif  // FNP_TRAITS_H_
