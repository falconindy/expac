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

#endif /* _ALPM_LIST_H */

/* vim: set et ts=2 sw=2: */
