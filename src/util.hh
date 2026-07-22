#ifndef _UTIL_H
#define _UTIL_H

#include <utility>

/* Runs a cleanup function at scope exit, replacing the GNU
 * __attribute__((cleanup)) extension with portable C++. Move-only (there's
 * only ever one thing running the cleanup); not copyable. */
template <typename F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F f) : f_(std::move(f)) {}
  ~ScopeGuard() { f_(); }

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&&) = delete;
  ScopeGuard& operator=(ScopeGuard&&) = delete;

 private:
  F f_;
};

template <typename F>
ScopeGuard<F> MakeScopeGuard(F f) {
  return ScopeGuard<F>(std::move(f));
}

#endif /* _UTIL_H */
