#ifndef _EXPAC_H
#define _EXPAC_H

#include <alpm.h>

#include <memory>
#include <vector>

typedef enum package_corpus_t {
  CORPUS_LOCAL,
  CORPUS_SYNC,
  CORPUS_FILE,
} package_corpus_t;

typedef enum search_what_t {
  SEARCH_EXACT,
  SEARCH_GROUPS,
  SEARCH_REGEX,
} search_what_t;

using PackageList = std::vector<alpm_pkg_t*>;

/* Owns an alpm_handle_t, initialized from a pacman.conf-style config file.
 * Only ever constructed via New(); the private constructor keeps callers
 * from ending up with a half-initialized instance on failure. Only ever
 * held via the unique_ptr New() returns, so it's neither copyable nor
 * movable -- there's no need to move the object itself, only the pointer.
 *
 * Also owns any packages loaded via SearchFiles() (alpm_pkg_load() results
 * belong to the caller, unlike -Q/-S results which belong to their db and
 * are released along with the handle) -- freeing them here in the
 * destructor means callers get a uniform PackageList back regardless of
 * corpus, with no per-corpus ownership bookkeeping of their own. */
class Expac {
 public:
  static std::unique_ptr<Expac> New(const char* config_file);

  ~Expac();

  Expac(const Expac&) = delete;
  Expac& operator=(const Expac&) = delete;
  Expac(Expac&&) = delete;
  Expac& operator=(Expac&&) = delete;

  PackageList SearchFiles(alpm_list_t* targets);
  PackageList SearchLocal(alpm_list_t* targets) const;
  PackageList SearchSync(alpm_list_t* targets) const;
  PackageList Search(package_corpus_t corpus, alpm_list_t* targets);

 private:
  explicit Expac(alpm_handle_t* alpm);

  alpm_handle_t* alpm_;
  PackageList owned_packages_;
};

#endif /* _EXPAC_H */

/* vim: set et ts=2 sw=2: */
