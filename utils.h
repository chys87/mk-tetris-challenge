#pragma once

#include <iterator>
#include <type_traits>
#include <utility>

// IMPORTANT: The following ctz/clz/bsr functions requires argument to be
// non-zero!!!
inline constexpr unsigned ctz(unsigned x) { return __builtin_ctz(x); }
inline constexpr unsigned ctz(unsigned long x) { return __builtin_ctzl(x); }
inline constexpr unsigned ctz(unsigned long long x) {
  return __builtin_ctzll(x);
}
inline constexpr unsigned ctz(int x) { return __builtin_ctz(x); }
inline constexpr unsigned ctz(long x) { return __builtin_ctzl(x); }
inline constexpr unsigned ctz(long long x) { return __builtin_ctzll(x); }
inline constexpr unsigned clz(unsigned x) { return __builtin_clz(x); }
inline constexpr unsigned clz(unsigned long x) { return __builtin_clzl(x); }
inline constexpr unsigned clz(unsigned long long x) {
  return __builtin_clzll(x);
}
inline constexpr unsigned clz(int x) { return __builtin_clz(x); }
inline constexpr unsigned clz(long x) { return __builtin_clzl(x); }
inline constexpr unsigned clz(long long x) { return __builtin_clzll(x); }

inline constexpr unsigned popcnt(unsigned x) { return __builtin_popcount(x); }
inline constexpr unsigned popcnt(unsigned long x) {
  return __builtin_popcountl(x);
}
inline constexpr unsigned popcnt(unsigned long long x) {
  return __builtin_popcountll(x);
}
inline constexpr unsigned popcnt(int x) { return __builtin_popcount(x); }
inline constexpr unsigned popcnt(long x) { return __builtin_popcountl(x); }
inline constexpr unsigned popcnt(long long x) {
  return __builtin_popcountll(x);
}

template <typename T>
requires std::is_unsigned_v<T>
inline constexpr T blsr(T x) { return (x & (x - 1)); }

// For use with range-based for
template <typename IT>
class IteratorRange {
 public:
  using iterator = IT;
  using difference_type = typename std::iterator_traits<IT>::difference_type;
  using size_type = std::make_unsigned_t<difference_type>;

  constexpr IteratorRange(IT lo, IT hi) : lo_(lo), hi_(hi) {}
  constexpr IteratorRange(IT lo, size_type n)
      : lo_(lo), hi_(std::next(lo, n)) {}

  constexpr IT begin() const { return lo_; }
  constexpr IT cbegin() const { return lo_; }
  constexpr IT end() const { return hi_; }
  constexpr IT cend() const { return hi_; }

  constexpr size_type size() const { return std::distance(lo_, hi_); }
  constexpr bool empty() const { return lo_ == hi_; }

 private:
  IT lo_, hi_;
};

// An iterator to iterate over set bits
template <typename T>
class BitIterator : public std::iterator<std::forward_iterator_tag, unsigned> {
 public:
  static_assert(std::is_unsigned_v<T>, "T must be an unsigned integral type");
  explicit constexpr BitIterator(T v = 0) : v_(v) {}
  BitIterator(const BitIterator &) = default;
  BitIterator &operator=(const BitIterator &) = default;

  unsigned operator*() const {
    return ctz(std::common_type_t<T, unsigned>(v_));
  }

  BitIterator &operator++() {
    v_ = blsr(v_);
    return *this;
  }
  BitIterator operator++(int) {
    T v = v_;
    v_ = blsr(v);
    return BitIterator(v);
  }

  friend constexpr bool operator==(const BitIterator &a, const BitIterator &b) {
    return a.v_ == b.v_;
  }

 private:
  T v_;
};

// 遍历一个整数中所有为1的二进制位
template <typename T>
inline constexpr IteratorRange<BitIterator<std::make_unsigned_t<T>>> set_bits(
    T v) {
  using UT = std::make_unsigned_t<T>;
  return {BitIterator<UT>(v), BitIterator<UT>(0)};
}

// Stolen from CityHash
template <class T>
void HashCombine(std::size_t &seed, const T &v) {
  std::hash<T> hasher;
  const std::size_t kMul = 0x9ddfea08eb382d69ULL;
  std::size_t a = (hasher(v) ^ seed) * kMul;
  a ^= (a >> 47);
  std::size_t b = (seed ^ a) * kMul;
  b ^= (b >> 47);
  seed = b * kMul;
}
