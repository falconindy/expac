/* Copyright (c) 2010-2014 Dave Reisner
 *
 * expac.c
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "expac.hh"

#include <alpm.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <glob.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "conf.hh"
#include "util.hh"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char const size_tokens[] = "BKMGTPEZYRQ";
static char const digits[] = "0123456789";
static char const printf_flags[] = "'-+ #0I";

static int is_valid_size_unit(char* u) {
  return u[0] != '\0' && u[1] == '\0' &&
         memchr(size_tokens, *u, sizeof(size_tokens) - 1) != NULL;
}

static std::string alpm_backup_get_name(alpm_backup_t* bkup) {
  return bkup->name;
}

static std::pair<double, std::string> humanize_size(off_t bytes,
                                                    const char target_unit) {
  static const int unitcount = sizeof(size_tokens) - 1;

  double val = (double)bytes;
  int index;

  for (index = 0; index < unitcount; index++) {
    if (target_unit != '\0' && size_tokens[index] == target_unit) {
      break;
    } else if (target_unit == '\0' && val <= 2048.0 && val >= -2048.0) {
      break;
    }
    val /= 1024.0;
  }

  std::string label(1, size_tokens[index]);
  if (size_tokens[index] != 'B') {
    label += "iB";
  }

  return {val, label};
}

static std::string size_to_string(off_t pkgsize, char humansize) {
  char out[64];

  if (humansize == 'B') {
    snprintf(out, sizeof(out), "%jd", (intmax_t)pkgsize);
  } else {
    const auto [n, unit] = humanize_size(pkgsize, humansize);
    snprintf(out, sizeof(out), "%.2f %s", n, unit.c_str());
  }

  return out;
}

static std::string format_optdep(alpm_depend_t* optdep) {
  return std::string(optdep->name) + ": " +
         (optdep->desc ? optdep->desc : "(null)");
}

static std::string alpm_dep_get_name(alpm_depend_t* dep) { return dep->name; }

static std::string dep_to_string(alpm_depend_t* dep) {
  char* s = alpm_dep_compute_string(dep);
  std::string result(s != NULL ? s : "");
  free(s);
  return result;
}

static void usage(void) {
  printf(
      "expac %s\n"
      "Usage: expac [options] <format> target...\n\n",
      PACKAGE_VERSION);
  printf(
      " Options:\n"
      "  -Q, --query               search local DB (default)\n"
      "  -S, --sync                search sync DBs\n"
      "  -s, --search              search for matching regex\n"
      "  -g, --group               return packages matching targets as groups\n"
      "  -H, --humansize <size>    format package sizes in SI units, or "
      "\"auto\"\n"
      "  -1, --readone             return only the first result of a sync "
      "search\n\n"
      "  -d, --delim <string>      separator used between packages (default: "
      "\"\\n\")\n"
      "  -l, --listdelim <string>  separator used between list elements "
      "(default: \"  \")\n"
      "  -p, --file                query local files instead of the DB\n"
      "  -t, --timefmt <fmt>       date format passed to strftime (default: "
      "\"%%c\")\n"
      "      --config <file>       read from <file> for alpm initialization "
      "(default: /etc/pacman.conf)\n\n"
      "  -v, --verbose             be more verbose\n\n"
      "  -V, --version             display version information and exit\n"
      "  -h, --help                display this help and exit\n\n"
      "For more details see expac(1).\n");
}

static void version(void) {
  printf("%s %s\n", program_invocation_short_name, PACKAGE_VERSION);
}

static int parse_options(int* argc, char** argv[], Options* opts) {
  static struct option long_opts[] = {{"readone", no_argument, 0, '1'},
                                      {"delim", required_argument, 0, 'd'},
                                      {"listdelim", required_argument, 0, 'l'},
                                      {"group", required_argument, 0, 'g'},
                                      {"help", no_argument, 0, 'h'},
                                      {"file", no_argument, 0, 'p'},
                                      {"humansize", required_argument, 0, 'H'},
                                      {"query", no_argument, 0, 'Q'},
                                      {"sync", no_argument, 0, 'S'},
                                      {"search", no_argument, 0, 's'},
                                      {"timefmt", required_argument, 0, 't'},
                                      {"verbose", no_argument, 0, 'v'},
                                      {"version", no_argument, 0, 'V'},
                                      {"config", required_argument, 0, 128},
                                      {0, 0, 0, 0}};

  for (;;) {
    int opt;

    opt = getopt_long(*argc, *argv, "1l:d:gH:hf:pQSst:Vv", long_opts, NULL);
    if (opt < 0) {
      break;
    }

    switch (opt) {
      case 'S':
        opts->corpus = CORPUS_SYNC;
        break;
      case 'Q':
        opts->corpus = CORPUS_LOCAL;
        break;
      case '1':
        opts->readone = true;
        break;
      case 'd':
        opts->delim = optarg;
        break;
      case 'g':
        opts->what = SEARCH_GROUPS;
        break;
      case 'l':
        opts->listdelim = optarg;
        break;
      case 'H':
        if (strcmp(optarg, "auto") == 0) {
          opts->humansize = '\0';
          break;
        }
        if (!is_valid_size_unit(optarg)) {
          fprintf(stderr, "error: invalid SI size formatter: %s\n", optarg);
          return -1;
        }
        opts->humansize = *optarg;
        break;
      case 'h':
        usage();
        exit(0);
      case 'p':
        opts->corpus = CORPUS_FILE;
        break;
      case 's':
        opts->what = SEARCH_REGEX;
        break;
      case 't':
        opts->timefmt = optarg;
        break;
      case 'V':
        version();
        exit(0);
        break;
      case 'v':
        opts->verbose = true;
        break;
      case 128:
        opts->config_file = optarg;
        break;

      case '?':
        return -EINVAL;
      default:
        return -EINVAL;
    }
  }

  if (optind < *argc) {
    opts->format = (*argv)[optind++];
  } else {
    fprintf(stderr, "error: missing format string (use -h for help)\n");
    return -EINVAL;
  }

  *argc -= optind;
  *argv += optind;

  return 0;
}

static int print_escaped(const char* delim) {
  const char* f;
  int out = 0;

  for (f = delim; *f != '\0'; f++) {
    if (*f == '\\') {
      switch (*++f) {
        case '\\':
          fputc('\\', stdout);
          break;
        case '"':
          fputc('\"', stdout);
          break;
        case 'a':
          fputc('\a', stdout);
          break;
        case 'b':
          fputc('\b', stdout);
          break;
        case 'e': /* \e is nonstandard */
          fputc('\033', stdout);
          break;
        case 'n':
          fputc('\n', stdout);
          break;
        case 'r':
          fputc('\r', stdout);
          break;
        case 't':
          fputc('\t', stdout);
          break;
        case 'v':
          fputc('\v', stdout);
          break;
        case '0':
          fputc('\0', stdout);
          break;
        default:
          fputc(*f, stdout);
          break;
      }
      ++out;
    } else {
      fputc(*f, stdout);
      ++out;
    }
  }

  return out;
}

/* Converts an alpm_list_t of T* into strings via fn(T*), e.g. turning a
 * list of alpm_depend_t* into their formatted name/version strings. The
 * static_cast<T*> here is the only cast involved -- unlike a function
 * pointer typedef'd to take void*, this is checked against fn's real
 * parameter type at compile time. */
template <typename T, typename Fn>
static std::vector<std::string> alpm_list_to_strings(alpm_list_t* list, Fn fn) {
  std::vector<std::string> result;

  for (alpm_list_t* i = list; i; i = i->next) {
    result.push_back(fn(static_cast<T*>(i->data)));
  }

  return result;
}

/* Same, for lists whose items are themselves plain C strings owned by
 * libalpm (e.g. licenses, groups) -- no per-item extraction needed. */
static std::vector<std::string> alpm_list_to_strings(alpm_list_t* list) {
  std::vector<std::string> result;

  for (alpm_list_t* i = list; i; i = i->next) {
    result.emplace_back(static_cast<const char*>(i->data));
  }

  return result;
}

/* Same, for the handful of alpm_pkg_compute_*() calls that return a list
 * we own outright (both the nodes and the strdup'd strings). */
static std::vector<std::string> alpm_owned_list_to_strings(alpm_list_t* list) {
  std::vector<std::string> result;

  for (alpm_list_t* i = list; i; i = i->next) {
    result.emplace_back(static_cast<char*>(i->data));
  }

  alpm_list_free_inner(list, free);
  alpm_list_free(list);

  return result;
}

static int print_list(const std::vector<std::string>& items,
                      const Options& opts) {
  int out = 0;

  if (items.empty()) {
    if (opts.verbose) {
      out += printf("None");
    }
    return out;
  }

  for (size_t i = 0; i < items.size(); ++i) {
    out += printf("%s", items[i].c_str());
    if (i + 1 < items.size()) {
      out += print_escaped(opts.listdelim);
    }
  }

  return out;
}

static int print_time(time_t timestamp, const Options& opts) {
  char buffer[64];
  int out = 0;

  if (!timestamp) {
    if (opts.verbose) {
      out += printf("None");
    }
    return out;
  }

  /* no overflow here, strftime prints a max of 64 including null */
  strftime(&buffer[0], 64, opts.timefmt, localtime(&timestamp));
  out += printf("%s", buffer);

  return out;
}

static int print_filelist(alpm_filelist_t* filelist, const Options& opts) {
  int out = 0;
  size_t i;

  for (i = 0; i < filelist->count; i++) {
    out += printf("%s", (filelist->files + i)->name);
    if (i < filelist->count - 1) {
      out += print_escaped(opts.listdelim);
    }
  }

  return out;
}

static bool backup_file_is_modified(const alpm_backup_t* backup_file) {
  char fullpath[PATH_MAX];
  char* md5sum = NULL;
  auto md5sum_guard = MakeScopeGuard([&] { free(md5sum); });
  bool modified;

  /* TODO: respect expac->dbroot */
  snprintf(fullpath, sizeof(fullpath), "/%s", backup_file->name);

  md5sum = alpm_compute_md5sum(fullpath);
  if (md5sum == NULL) {
    return false;
  }

  modified = strcmp(md5sum, backup_file->hash) != 0;

  return modified;
}

static std::vector<std::string> get_modified_files(alpm_pkg_t* pkg) {
  std::vector<std::string> modified_files;

  for (alpm_list_t* i = alpm_pkg_get_backup(pkg); i; i = i->next) {
    const alpm_backup_t* backup = static_cast<const alpm_backup_t*>(i->data);
    if (backup->hash && backup_file_is_modified(backup)) {
      modified_files.emplace_back(backup->name);
    }
  }

  return modified_files;
}

static std::vector<std::string> get_validation_method(alpm_pkg_t* pkg) {
  alpm_pkgvalidation_t v =
      static_cast<alpm_pkgvalidation_t>(alpm_pkg_get_validation(pkg));

  if (v == ALPM_PKG_VALIDATION_UNKNOWN) {
    return {"Unknown"};
  }

  if (v & ALPM_PKG_VALIDATION_NONE) {
    return {"None"};
  }

  std::vector<std::string> validation;

  if (v & ALPM_PKG_VALIDATION_MD5SUM) {
    validation.emplace_back("MD5 Sum");
  }
  if (v & ALPM_PKG_VALIDATION_SHA256SUM) {
    validation.emplace_back("SHA256 Sum");
  }
  if (v & ALPM_PKG_VALIDATION_SIGNATURE) {
    validation.emplace_back("Signature");
  }

  return validation;
}

static void print_pkg(alpm_pkg_t* pkg, Options& opts) {
  const char *f, *end;
  int out = 0;

  end = opts.format + strlen(opts.format);

  for (f = opts.format; f < end; f++) {
    if (*f == '%') {
      char fmt[64] = {0};
      int l = 1;

      l += strspn(f + l, printf_flags);
      l += strspn(f + l, digits);
      memcpy(fmt, f, l);
      fmt[l] = 's';

      f += l;
      switch (*f) {
        /* simple attributes */
        case 'f': /* filename */
          out += printf(fmt, alpm_pkg_get_filename(pkg));
          break;
        case 'e': /* package base */
          out += printf(fmt, alpm_pkg_get_base(pkg));
          break;
        case 'n': /* package name */
          out += printf(fmt, alpm_pkg_get_name(pkg));
          break;
        case 'v': /* version */
          out += printf(fmt, alpm_pkg_get_version(pkg));
          break;
        case 'd': /* description */
          out += printf(fmt, alpm_pkg_get_desc(pkg));
          break;
        case 'u': /* project url */
          out += printf(fmt, alpm_pkg_get_url(pkg));
          break;
        case 'p': /* packager name */
          out += printf(fmt, alpm_pkg_get_packager(pkg));
          break;
        case 's': /* md5sum */
          out += printf(fmt, alpm_pkg_get_md5sum(pkg));
          break;
        case 'a': /* architecture */
          out += printf(fmt, alpm_pkg_get_arch(pkg));
          break;
        case 'i': /* has install scriptlet? */
          out += printf(fmt, alpm_pkg_has_scriptlet(pkg) ? "yes" : "no");
          break;
        case 'r': /* repo */
          out += printf(fmt, alpm_db_get_name(alpm_pkg_get_db(pkg)));
          break;
        case 'w': /* install reason */
          out +=
              printf(fmt, alpm_pkg_get_reason(pkg) ? "dependency" : "explicit");
          break;
        case '!': /* result number */
          fmt[strlen(fmt) - 1] = 'd';
          out += printf(fmt, opts.pkgcounter++);
          break;
        case 'g': /* base64 gpg sig */
          out += printf(fmt, alpm_pkg_get_base64_sig(pkg));
          break;
        case 'h': /* sha256sum */
          out += printf(fmt, alpm_pkg_get_sha256sum(pkg));
          break;

        /* times */
        case 'b': /* build date */
          out += print_time(alpm_pkg_get_builddate(pkg), opts);
          break;
        case 'l': /* install date */
          out += print_time(alpm_pkg_get_installdate(pkg), opts);
          break;

        /* sizes */
        case 'k': /* download size */
          out += printf(
              fmt,
              size_to_string(alpm_pkg_get_size(pkg), opts.humansize).c_str());
          break;
        case 'm': /* install size */
          out += printf(
              fmt,
              size_to_string(alpm_pkg_get_isize(pkg), opts.humansize).c_str());
          break;

        /* lists */
        case 'F': /* files */
          out += print_filelist(alpm_pkg_get_files(pkg), opts);
          break;
        case 'N': /* requiredby */
          out += print_list(
              alpm_owned_list_to_strings(alpm_pkg_compute_requiredby(pkg)),
              opts);
          break;
        case 'W': /* optionalfor */
          out += print_list(
              alpm_owned_list_to_strings(alpm_pkg_compute_optionalfor(pkg)),
              opts);
          break;
        case 'L': /* licenses */
          out += print_list(alpm_list_to_strings(alpm_pkg_get_licenses(pkg)),
                            opts);
          break;
        case 'G': /* groups */
          out +=
              print_list(alpm_list_to_strings(alpm_pkg_get_groups(pkg)), opts);
          break;
        case 'E': /* depends (shortdeps) */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_depends(pkg), alpm_dep_get_name),
                            opts);
          break;
        case 'J': /* makedepends */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_makedepends(pkg), dep_to_string),
                            opts);
          break;
        case 'K': /* checkdepends */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_checkdepends(pkg), dep_to_string),
                            opts);
          break;
        case 'D': /* depends */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_depends(pkg), dep_to_string),
                            opts);
          break;
        case 'O': /* optdepends */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_optdepends(pkg), format_optdep),
                            opts);
          break;
        case 'o': /* optdepends (shortdeps) */
          out +=
              print_list(alpm_list_to_strings<alpm_depend_t>(
                             alpm_pkg_get_optdepends(pkg), alpm_dep_get_name),
                         opts);
          break;
        case 'H': /* conflicts */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_conflicts(pkg), dep_to_string),
                            opts);
          break;
        case 'C': /* conflicts (shortdeps) */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_conflicts(pkg), alpm_dep_get_name),
                            opts);
          break;
        case 'S': /* provides (shortdeps) */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_provides(pkg), alpm_dep_get_name),
                            opts);
          break;
        case 'P': /* provides */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_provides(pkg), dep_to_string),
                            opts);
          break;
        case 'R': /* replaces (shortdeps) */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_replaces(pkg), alpm_dep_get_name),
                            opts);
          break;
        case 'T': /* replaces */
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
                                alpm_pkg_get_replaces(pkg), dep_to_string),
                            opts);
          break;
        case 'B': /* backup */
          out += print_list(alpm_list_to_strings<alpm_backup_t>(
                                alpm_pkg_get_backup(pkg), alpm_backup_get_name),
                            opts);
          break;
        case 'V': /* package validation */
          out += print_list(get_validation_method(pkg), opts);
          break;
        case 'M': /* modified */
          out += print_list(get_modified_files(pkg), opts);
          break;
        case '%':
          fputc('%', stdout);
          out++;
          break;
        default:
          fputc('?', stdout);
          out++;
          break;
      }
    } else if (*f == '\\') {
      char esc[3] = {f[0], f[1], '\0'};
      out += print_escaped(esc);
      ++f;
    } else {
      fputc(*f, stdout);
      out++;
    }
  }

  /* only print a delimeter if any package data was outputted */
  if (out > 0) {
    print_escaped(opts.delim);
  }
}

static PackageList all_packages(alpm_list_t* dbs) {
  PackageList packages;

  for (alpm_list_t* i = dbs; i; i = i->next) {
    alpm_db_t* db = static_cast<alpm_db_t*>(i->data);
    for (alpm_list_t* j = alpm_db_get_pkgcache(db); j; j = j->next) {
      packages.push_back(static_cast<alpm_pkg_t*>(j->data));
    }
  }

  return packages;
}

static PackageList search_packages(alpm_list_t* dbs, alpm_list_t* targets) {
  PackageList packages;

  for (alpm_list_t* i = dbs; i; i = i->next) {
    alpm_list_t* results = NULL;
    alpm_db_search(static_cast<alpm_db_t*>(i->data), targets, &results);
    for (alpm_list_t* j = results; j; j = j->next) {
      packages.push_back(static_cast<alpm_pkg_t*>(j->data));
    }
    alpm_list_free(results);
  }

  return packages;
}

static PackageList search_groups(alpm_list_t* dbs, alpm_list_t* groupnames) {
  PackageList packages;

  for (alpm_list_t* i = groupnames; i; i = i->next) {
    for (alpm_list_t* j = dbs; j; j = j->next) {
      alpm_group_t* grp = alpm_db_get_group(static_cast<alpm_db_t*>(j->data),
                                            static_cast<const char*>(i->data));
      if (grp != NULL) {
        for (alpm_list_t* k = grp->packages; k; k = k->next) {
          packages.push_back(static_cast<alpm_pkg_t*>(k->data));
        }
      }
    }
  }

  return packages;
}

static PackageList search_exact(alpm_list_t* dblist, alpm_list_t* targets,
                                const Options& opts) {
  PackageList results;

  /* resolve each target individually from the repo pool */
  for (alpm_list_t* t = targets; t; t = t->next) {
    char *pkgname, *reponame;
    int found = 0;

    pkgname = reponame = static_cast<char*>(t->data);
    if (strchr(pkgname, '/')) {
      strsep(&pkgname, "/");
    } else {
      reponame = NULL;
    }

    for (alpm_list_t* r = dblist; r; r = r->next) {
      alpm_db_t* repo = static_cast<alpm_db_t*>(r->data);
      alpm_pkg_t* pkg;

      if (reponame && strcmp(reponame, alpm_db_get_name(repo)) != 0) {
        continue;
      }

      pkg = alpm_db_get_pkg(repo, pkgname);
      if (pkg == NULL) {
        continue;
      }

      found = 1;
      results.push_back(pkg);
      if (opts.readone) {
        break;
      }
    }

    if (!found && opts.verbose) {
      fprintf(stderr, "error: package `%s' not found\n", pkgname);
    }
  }

  return results;
}

static PackageList resolve_targets(alpm_list_t* dblist, alpm_list_t* targets,
                                   const Options& opts) {
  if (targets == NULL) {
    return all_packages(dblist);
  }

  if (opts.what == SEARCH_REGEX) {
    return search_packages(dblist, targets);
  }

  if (opts.what == SEARCH_GROUPS) {
    return search_groups(dblist, targets);
  }

  return search_exact(dblist, targets, opts);
}

Expac::Expac(alpm_handle_t* alpm) : alpm_(alpm) {}

Expac::~Expac() {
  for (alpm_pkg_t* pkg : owned_packages_) {
    alpm_pkg_free(pkg);
  }

  alpm_release(alpm_);
}

std::unique_ptr<Expac> Expac::New(const char* config_file) {
  config_t config;

  if (config_parse(&config, config_file) < 0) {
    return nullptr;
  }

  const char* dbroot = "/";
  const char* dbpath = "/var/lib/pacman";

  if (config.dbpath) {
    dbpath = config.dbpath->c_str();
  }

  if (config.dbroot) {
    dbroot = config.dbroot->c_str();
  }

  enum _alpm_errno_t alpm_errno = static_cast<enum _alpm_errno_t>(0);
  alpm_handle_t* alpm = alpm_initialize(dbroot, dbpath, &alpm_errno);
  if (alpm == nullptr) {
    fprintf(stderr, "error: failed to initialize alpm: %s\n",
            alpm_strerror(alpm_errno));
    return nullptr;
  }

  for (const auto& repo : config.repos) {
    alpm_register_syncdb(alpm, repo.c_str(), 0);
  }

  return std::unique_ptr<Expac>(new Expac(alpm));
}

PackageList Expac::SearchFiles(alpm_list_t* targets) {
  PackageList r;

  for (alpm_list_t* i = targets; i; i = i->next) {
    const char* path = static_cast<const char*>(i->data);
    alpm_pkg_t* pkg;

    if (alpm_pkg_load(alpm_, path, 0, 0, &pkg) != 0) {
      fprintf(stderr, "error: %s: %s\n", path,
              alpm_strerror(alpm_errno(alpm_)));
      continue;
    }

    owned_packages_.push_back(pkg);
    r.push_back(pkg);
  }

  return r;
}

PackageList Expac::SearchLocal(alpm_list_t* targets,
                               const Options& opts) const {
  alpm_list_t* dblist = alpm_list_add(NULL, alpm_get_localdb(alpm_));
  PackageList r = resolve_targets(dblist, targets, opts);
  alpm_list_free(dblist);

  return r;
}

PackageList Expac::SearchSync(alpm_list_t* targets, const Options& opts) const {
  return resolve_targets(alpm_get_syncdbs(alpm_), targets, opts);
}

PackageList Expac::Search(alpm_list_t* targets, const Options& opts) {
  switch (opts.corpus) {
    case CORPUS_LOCAL:
      return SearchLocal(targets, opts);
    case CORPUS_SYNC:
      return SearchSync(targets, opts);
    case CORPUS_FILE:
      return SearchFiles(targets);
  }

  /* should be unreachable */
  return {};
}

static int read_targets_from_file(FILE* in, alpm_list_t** targets) {
  char line[BUFSIZ];
  int i = 0, end = 0, targets_added = 0;

  while (!end) {
    line[i] = fgetc(in);

    if (feof(in)) end = 1;

    if (isspace(line[i]) || end) {
      line[i] = '\0';
      /* avoid adding zero length arg, if multiple spaces separate args */
      if (i > 0) {
        if (!alpm_list_find_str(*targets, line)) {
          *targets = alpm_list_add(*targets, strdup(line));
        }
        i = 0;
        ++targets_added;
      }
    } else {
      ++i;
      if (i >= BUFSIZ) {
        fprintf(stderr, "error: buffer overflow on stdin\n");
        return -1;
      }
    }
  }

  return targets_added;
}

static int process_targets(int argc, char** argv, alpm_list_t** targets) {
  int allow_stdin;

  allow_stdin = !isatty(STDIN_FILENO);

  for (int i = 0; i < argc; ++i) {
    if (allow_stdin && strcmp(argv[i], "-") == 0) {
      int k;

      k = read_targets_from_file(stdin, targets);
      if (k < 0) {
        return k;
      }

      if (k == 0) {
        fputs("error: argument '-' specified with empty stdin\n", stderr);
        return -1;
      }

      allow_stdin = 0;
    } else {
      *targets = alpm_list_add(*targets, strdup(argv[i]));
    }
  }

  return 0;
}

int main(int argc, char* argv[]) {
  Options opts;
  alpm_list_t* targets = NULL;
  auto targets_guard = MakeScopeGuard([&] {
    alpm_list_free_inner(targets, free);
    alpm_list_free(targets);
  });
  int r;

  r = parse_options(&argc, &argv, &opts);
  if (r < 0) {
    return 1;
  }

  r = process_targets(argc, argv, &targets);
  if (r < 0) {
    return 1;
  }

  std::unique_ptr<Expac> expac = Expac::New(opts.config_file);
  if (!expac) {
    return 1;
  }

  /* packages loaded via -p/--file are owned by expac (freed in ~Expac);
   * -Q/-S results belong to their db and are released along with the
   * handle. Either way, results itself is just a vector of pointers we
   * don't own -- nothing to free here. */
  PackageList results = expac->Search(targets, opts);
  if (results.empty()) {
    return 1;
  }

  for (alpm_pkg_t* pkg : results) {
    print_pkg(pkg, opts);
  }

  return 0;
}

/* vim: set et ts=2 sw=2: */
