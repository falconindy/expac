// SPDX-License-Identifier: MIT

#include "expac.hh"

#include <alpm.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <glob.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "alpm_list.hh"
#include "conf.hh"
#include "package_formatter.hh"

static char const size_tokens[] = "BKMGTPEZYRQ";

static int is_valid_size_unit(char* u) {
  return u[0] != '\0' && u[1] == '\0' &&
         memchr(size_tokens, *u, sizeof(size_tokens) - 1) != NULL;
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

static PackageList all_packages(alpm_list_t* dbs) {
  PackageList packages;

  for (alpm_db_t* db : AlpmList<alpm_db_t*>(dbs)) {
    for (alpm_pkg_t* pkg : AlpmList<alpm_pkg_t*>(alpm_db_get_pkgcache(db))) {
      packages.push_back(pkg);
    }
  }

  return packages;
}

static PackageList search_packages(alpm_list_t* dbs,
                                   const std::vector<std::string>& targets) {
  PackageList packages;

  // alpm_db_search() takes its needles as an alpm_list_t; build one that
  // just borrows the vector's strings for the duration of the call.
  ScopedAlpmList needles(targets, [](const std::string& target) {
    return const_cast<char*>(target.c_str());
  });

  for (alpm_db_t* db : AlpmList<alpm_db_t*>(dbs)) {
    alpm_list_t* results = NULL;
    alpm_db_search(db, needles.to_list(), &results);
    for (alpm_pkg_t* pkg : AlpmList<alpm_pkg_t*>(results)) {
      packages.push_back(pkg);
    }
    alpm_list_free(results);
  }

  return packages;
}

static PackageList search_groups(alpm_list_t* dbs,
                                 const std::vector<std::string>& groupnames) {
  PackageList packages;

  for (const std::string& name : groupnames) {
    for (alpm_db_t* db : AlpmList<alpm_db_t*>(dbs)) {
      alpm_group_t* grp = alpm_db_get_group(db, name.c_str());
      if (grp != NULL) {
        for (alpm_pkg_t* pkg : AlpmList<alpm_pkg_t*>(grp->packages)) {
          packages.push_back(pkg);
        }
      }
    }
  }

  return packages;
}

static PackageList search_exact(alpm_list_t* dblist,
                                const std::vector<std::string>& targets,
                                const Options& opts) {
  PackageList results;

  // resolve each target individually from the repo pool
  for (std::string target : targets) {
    char* pkgname = target.data();
    char* reponame = pkgname;
    bool found = false;

    if (strchr(pkgname, '/')) {
      strsep(&pkgname, "/");
    } else {
      reponame = NULL;
    }

    for (alpm_db_t* repo : AlpmList<alpm_db_t*>(dblist)) {
      alpm_pkg_t* pkg;

      if (reponame && strcmp(reponame, alpm_db_get_name(repo)) != 0) {
        continue;
      }

      pkg = alpm_db_get_pkg(repo, pkgname);
      if (pkg == NULL) {
        continue;
      }

      found = true;
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

static PackageList resolve_targets(alpm_list_t* dblist,
                                   const std::vector<std::string>& targets,
                                   const Options& opts) {
  if (targets.empty()) {
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

PackageList Expac::SearchFiles(const std::vector<std::string>& targets) {
  PackageList r;

  for (const std::string& path : targets) {
    alpm_pkg_t* pkg;

    if (alpm_pkg_load(alpm_, path.c_str(), 0, 0, &pkg) != 0) {
      fprintf(stderr, "error: %s: %s\n", path.c_str(),
              alpm_strerror(alpm_errno(alpm_)));
      continue;
    }

    owned_packages_.push_back(pkg);
    r.push_back(pkg);
  }

  return r;
}

PackageList Expac::SearchLocal(const std::vector<std::string>& targets,
                               const Options& opts) const {
  alpm_list_t* dblist = alpm_list_add(NULL, alpm_get_localdb(alpm_));
  PackageList r = resolve_targets(dblist, targets, opts);
  alpm_list_free(dblist);

  return r;
}

PackageList Expac::SearchSync(const std::vector<std::string>& targets,
                              const Options& opts) const {
  return resolve_targets(alpm_get_syncdbs(alpm_), targets, opts);
}

PackageList Expac::Search(const std::vector<std::string>& targets,
                          const Options& opts) {
  switch (opts.corpus) {
    case CORPUS_LOCAL:
      return SearchLocal(targets, opts);
    case CORPUS_SYNC:
      return SearchSync(targets, opts);
    case CORPUS_FILE:
      return SearchFiles(targets);
  }

  // should be unreachable
  return {};
}

static int read_targets_from_file(FILE* in, std::vector<std::string>* targets) {
  char line[BUFSIZ];
  int i = 0, end = 0, targets_added = 0;

  while (!end) {
    line[i] = fgetc(in);

    if (feof(in)) end = 1;

    if (isspace(line[i]) || end) {
      line[i] = '\0';
      // avoid adding zero length arg, if multiple spaces separate args
      if (i > 0) {
        if (std::find(targets->begin(), targets->end(), line) ==
            targets->end()) {
          targets->emplace_back(line);
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

static int process_targets(int argc, char** argv,
                           std::vector<std::string>* targets) {
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
      targets->emplace_back(argv[i]);
    }
  }

  return 0;
}

int main(int argc, char* argv[]) {
  Options opts;
  std::vector<std::string> targets;
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

  // packages loaded via -p/--file are owned by expac (freed in ~Expac);
  // -Q/-S results belong to their db and are released along with the
  // handle. Either way, results itself is just a vector of pointers we
  // don't own -- nothing to free here.
  PackageList results = expac->Search(targets, opts);
  if (results.empty()) {
    return 1;
  }

  PackageFormatter formatter(opts);
  for (alpm_pkg_t* pkg : results) {
    formatter.Print(pkg);
  }

  return 0;
}

// vim: set et ts=2 sw=2:
