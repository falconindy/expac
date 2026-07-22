#ifndef _EXPAC_H
#define _EXPAC_H

#include <alpm.h>

#include <memory>

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

/* Owns an alpm_handle_t, initialized from a pacman.conf-style config file.
 * Only ever constructed via New(); the private constructor keeps callers
 * from ending up with a half-initialized instance on failure. */
class Expac {
 public:
  static std::unique_ptr<Expac> New(const char* config_file);

  ~Expac();

  Expac(const Expac&) = delete;
  Expac& operator=(const Expac&) = delete;
  Expac(Expac&& other) noexcept;
  Expac& operator=(Expac&& other) noexcept;

  alpm_list_t* SearchFiles(alpm_list_t* targets) const;
  alpm_list_t* SearchLocal(alpm_list_t* targets) const;
  alpm_list_t* SearchSync(alpm_list_t* targets) const;
  alpm_list_t* Search(package_corpus_t corpus, alpm_list_t* targets) const;

 private:
  explicit Expac(alpm_handle_t* alpm);

  alpm_handle_t* alpm_;
};

#endif /* _EXPAC_H */

/* vim: set et ts=2 sw=2: */
