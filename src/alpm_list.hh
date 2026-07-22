#ifndef _ALPM_LIST_H
#define _ALPM_LIST_H

#include <alpm.h>

/* Non-owning range-for view over an alpm_list_t, so callers can write
 * `for (T item : AlpmList<T>(list)) { ... }` instead of manually walking
 * ->next and casting ->data by hand. Read-only: it neither builds nor
 * frees the underlying list. */
template <typename T>
class AlpmList {
 public:
  class iterator {
   public:
    explicit iterator(alpm_list_t* node) : node_(node) {}

    T operator*() const { return static_cast<T>(node_->data); }
    iterator& operator++() {
      node_ = node_->next;
      return *this;
    }
    bool operator!=(const iterator& other) const {
      return node_ != other.node_;
    }

   private:
    alpm_list_t* node_;
  };

  explicit AlpmList(alpm_list_t* list) : list_(list) {}

  iterator begin() const { return iterator(list_); }
  iterator end() const { return iterator(nullptr); }

 private:
  alpm_list_t* list_;
};

/* Owns the *nodes* of a freshly built alpm_list_t (never the items they
 * point to), for call sites that need to hand libalpm a list built from
 * some other container -- e.g. alpm_db_search()'s needle list built from
 * a std::vector<std::string>. fn projects each container element to the
 * void* libalpm expects. Implicitly converts to alpm_list_t* so it can be
 * passed straight to alpm_list_t*-taking APIs. */
class ScopedAlpmList {
 public:
  template <typename Container, typename Fn>
  ScopedAlpmList(const Container& items, Fn fn) {
    for (const auto& item : items) {
      list_ = alpm_list_add(list_, fn(item));
    }
  }

  ~ScopedAlpmList() { alpm_list_free(list_); }

  ScopedAlpmList(const ScopedAlpmList&) = delete;
  ScopedAlpmList& operator=(const ScopedAlpmList&) = delete;

  operator alpm_list_t*() const { return list_; }

 private:
  alpm_list_t* list_ = nullptr;
};

#endif /* _ALPM_LIST_H */

/* vim: set et ts=2 sw=2: */
