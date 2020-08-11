#ifndef SLOPE_DEBUG_H_
#define SLOPE_DEBUG_H_

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#ifdef SLOPE_DEBUG
#define deb(x) std::cout << #x << ": " << (x) << std::endl
#  define deb2(x, y) std::cout << #x << ": " << (x) << ", " << #y << ": " << (y) << std::endl
#  define debout(x) std::cout << x << std::endl
#  define debline() std::cout << std::endl
#define infoout(x) std::cout << "[INFO]    " << x << std::endl
#else
#define NOOP \
  do {       \
  } while (0)
#define deb(x) NOOP
#define deb2(x, y) NOOP
#define debout(x) NOOP
#define debline() NOOP
#define infoout(x) NOOP
#endif  // SLOPE_DEBUG

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

template<typename T, typename Alloc = std::allocator<T>>
std::ostream& operator<<(std::ostream& os, const std::set<T, Alloc>& v) {
  os<< "{";
  int first = 1;
  for(const auto& it: v) {
    if(!first) {
      os << ", ";
    }
    first = 0;
    os << it;
  }
  os << "}";
  return os;
}

#define assert_p(cond, msg) do { \
  if(!(cond)) { \
    perror(msg); \
    std::abort(); \
  } \
} while(false);

#define prompt(x) do { \
  std::string _; \
  std::cout << (x) << std::endl; \
  std::cin >> _; \
} while(false);

template<typename... Args>
struct Typer;

#endif  // SLOPE_DEBUG_H_
