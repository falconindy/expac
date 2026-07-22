#ifndef _EXPAC_H
#define _EXPAC_H

#include <alpm.h>

#include <memory>
#include <string>
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

/* Parsed command-line options. Threaded through wherever search/formatting
 * behavior needs to vary at runtime, instead of a pile of globals. */
struct Options {
  bool readone = false;
  bool verbose = false;
  char humansize = 'B';
  package_corpus_t corpus = CORPUS_LOCAL;
  search_what_t what = SEARCH_EXACT;
  const char* format = nullptr;
  const char* timefmt = "%c";
  const char* listdelim = "  ";
  const char* delim = "\n";
  const char* config_file = "/etc/pacman.conf";
  int pkgcounter = 0;
};

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

  PackageList SearchFiles(const std::vector<std::string>& targets);
  PackageList SearchLocal(const std::vector<std::string>& targets,
                          const Options& opts) const;
  PackageList SearchSync(const std::vector<std::string>& targets,
                         const Options& opts) const;
  PackageList Search(const std::vector<std::string>& targets,
                     const Options& opts);

 private:
  explicit Expac(alpm_handle_t* alpm);

  alpm_handle_t* alpm_;
  PackageList owned_packages_;
};

#endif /* _EXPAC_H */

/* vim: set et ts=2 sw=2: */
