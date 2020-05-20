#ifndef SLOPE_DEBUG_H_
#define SLOPE_DEBUG_H_

#include <iostream>
#include <vector>
#include <map>
#include <algorithm>

#ifdef SLOPE_DEBUG
#  define deb(x) std::cout << #x << ": " << (x) << std::endl
#  define debout(x) std::cout << x << std::endl
#  define debline() std::cout << std::endl

template<typename T, typename T2>
std::ostream& operator<<(std::ostream& os, const std::pair<T, T2>& p) {
  return os << "(" << p.first << ", " << p.second << ")";
}

template<typename T, typename T2>
std::ostream& operator<<(std::ostream& os, const std::map<T, T2>& mp) {
  os<< "{";
  int first = 1;
  for(const auto& it: mp) {
    if(!first) {
      os << ", ";
    }
    first = 0;
    os << it.first << ": " << it.second;
  }
  os << "}";
  return os;
}

template<typename T, typename Alloc = std::allocator<T>>
std::ostream& operator<<(std::ostream& os, const std::vector<T, Alloc>& v) {
  os<< "[";
  int first = 1;
  for(const auto& it: v) {
    if(!first) {
      os << ", ";
    }
    first = 0;
    os << it;
  }
  os << "]";
  return os;
}

#else
#  define deb(x) do { } while(0);
#  define debout(x) do { } while(0);
#  define debline() do { } while(0);
#endif  // SLOPE_DEBUG

#endif  // SLOPE_DEBUG_H_
