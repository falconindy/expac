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

#include <alpm.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <glob.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "expac.h"
#include "conf.h"
#include "util.h"

#define DEFAULT_DELIM        "\n"
#define DEFAULT_LISTDELIM    "  "
#define DEFAULT_TIMEFMT      "%c"
#define SIZE_TOKENS          "BKMGTPEZY\0"

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif

static char const digits[] = "0123456789";
static char const printf_flags[] = "'-+ #0I";

bool opt_readone = false;
bool opt_verbose = false;
char opt_humansize = 'B';
package_corpus_t opt_corpus = CORPUS_LOCAL;
search_what_t opt_what = SEARCH_EXACT;
const char *opt_format = NULL;
const char *opt_timefmt = DEFAULT_TIMEFMT;
const char *opt_listdelim = DEFAULT_LISTDELIM;
const char *opt_delim = DEFAULT_DELIM;
const char *opt_config_file = "/etc/pacman.conf";
int opt_pkgcounter = 0;

typedef const char *(*extractfn)(void*);

static int is_valid_size_unit(char *u)
{
  return u[0] != '\0' && u[1] == '\0' &&
    memchr(SIZE_TOKENS, *u, strlen(SIZE_TOKENS)) != NULL;
}

static const char *alpm_backup_get_name(alpm_backup_t *bkup)
{
  return bkup->name;
}

static double humanize_size(off_t bytes, const char target_unit,
    const char **label)
{
  static const char *labels[] = {"B", "KiB", "MiB", "GiB",
    "TiB", "PiB", "EiB", "ZiB", "YiB"};
  static const int unitcount = sizeof(labels) / sizeof(labels[0]);

  double val = (double)bytes;
  int index;

  for(index = 0; index < unitcount - 1; index++) {
    if(target_unit != '\0' && labels[index][0] == target_unit) {
      break;
    } else if(target_unit == '\0' && val <= 2048.0 && val >= -2048.0) {
      break;
    }
    val /= 1024.0;
  }

  if(label) {
    *label = labels[index];
  }

  return val;
}

static char *size_to_string(off_t pkgsize)
{
  static char out[64];

  if(opt_humansize == 'B') {
    snprintf(out, sizeof(out), "%jd", (intmax_t)pkgsize);
  } else {
    snprintf(out, sizeof(out), "%.2f %ciB", humanize_size(pkgsize, opt_humansize, NULL), opt_humansize);
  }

  return out;
}

static char *format_optdep(alpm_depend_t *optdep)
{
  char *out = NULL;

  if(asprintf(&out, "%s: %s", optdep->name, optdep->desc) < 0) {
    return NULL;
  }

  return out;
}

static const char *alpm_dep_get_name(alpm_depend_t *dep)
{
  return dep->name;
}

static void usage(void)
{
  fprintf(stderr, "expac %s\n"
      "Usage: expac [options] <format> target...\n\n", VERSION);
  fprintf(stderr,
      " Options:\n"
      "  -Q, --query               search local DB (default)\n"
      "  -S, --sync                search sync DBs\n"
      "  -s, --search              search for matching regex\n"
      "  -g, --group               return packages matching targets as groups\n"
      "  -H, --humansize <size>    format package sizes in SI units (default: bytes)\n"
      "  -1, --readone             return only the first result of a sync search\n\n"
      "  -d, --delim <string>      separator used between packages (default: \"\\n\")\n"
      "  -l, --listdelim <string>  separator used between list elements (default: \"  \")\n"
      "  -p, --file                query local files instead of the DB\n"
      "  -t, --timefmt <fmt>       date format passed to strftime (default: \"%%c\")\n"
      "      --config <file>       read from <file> for alpm initialization (default: /etc/pacman.conf)\n\n"
      "  -v, --verbose             be more verbose\n\n"
      "  -h, --help                display this help and exit\n\n"
      "For more details see expac(1).\n");
}

static int parse_options(int *argc, char **argv[])
{
  static struct option opts[] = {
    {"readone",   no_argument,        0, '1'},
    {"delim",     required_argument,  0, 'd'},
    {"listdelim", required_argument,  0, 'l'},
    {"group",     required_argument,  0, 'g'},
    {"help",      no_argument,        0, 'h'},
    {"file",      no_argument,        0, 'p'},
    {"humansize", required_argument,  0, 'H'},
    {"query",     no_argument,        0, 'Q'},
    {"sync",      no_argument,        0, 'S'},
    {"search",    no_argument,        0, 's'},
    {"timefmt",   required_argument,  0, 't'},
    {"verbose",   no_argument,        0, 'v'},
    {"config",    required_argument,  0, 128},
    {0, 0, 0, 0}
  };

  for(;;) {
    int opt;

    opt = getopt_long(*argc, *argv, "1l:d:gH:hf:pQSst:v", opts, NULL);
    if(opt < 0) {
      break;
    }

    switch (opt) {
      case 'S':
        opt_corpus = CORPUS_SYNC;
        break;
      case 'Q':
        opt_corpus = CORPUS_LOCAL;
        break;
      case '1':
        opt_readone = true;
        break;
      case 'd':
        opt_delim = optarg;
        break;
      case 'g':
        opt_what = SEARCH_GROUPS;
        break;
      case 'l':
        opt_listdelim = optarg;
        break;
      case 'H':
        if(!is_valid_size_unit(optarg)) {
          fprintf(stderr, "error: invalid SI size formatter: %s\n", optarg);
          return 1;
        }
        opt_humansize = *optarg;
        break;
      case 'h':
        usage();
        exit(0);
      case 'p':
        opt_corpus = CORPUS_FILE;
        break;
      case 's':
        opt_what = SEARCH_REGEX;
        break;
      case 't':
        opt_timefmt = optarg;
        break;
      case 'v':
        opt_verbose = true;
        break;
      case 128:
        opt_config_file = optarg;
        break;

      case '?':
        return -EINVAL;
      default:
        return -EINVAL;
    }
  }

  if(optind < *argc) {
    opt_format = (*argv)[optind++];
  } else {
    fprintf(stderr, "error: missing format string (use -h for help)\n");
    return -EINVAL;
  }

  *argc -= optind;
  *argv += optind;

  return 0;
}

static int print_escaped(const char *delim)
{
  const char *f;
  int out = 0;

  for(f = delim; *f != '\0'; f++) {
    if(*f == '\\') {
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

static int print_list(alpm_list_t *list, extractfn fn)
{
  alpm_list_t *i;
  int out = 0;

  if(!list) {
    if(opt_verbose) {
      out += printf("None");
    }
    return out;
  }

  i = list;
  for(;;) {
    const char *item = fn ? fn(i->data) : i->data;
    if(item == NULL) {
      continue;
    }

    out += printf("%s", item);

    if((i = i->next)) {
      out += print_escaped(opt_listdelim);
    } else {
      break;
    }
  }

  return out;
}

static int print_allocated_list(alpm_list_t *list, extractfn fn)
{
  int out = print_list(list, fn);
  alpm_list_free(list);
  return out;
}

static int print_time(time_t timestamp) {
  char buffer[64];
  int out = 0;

  if(!timestamp) {
    if(opt_verbose) {
      out += printf("None");
    }
    return out;
  }

  /* no overflow here, strftime prints a max of 64 including null */
  strftime(&buffer[0], 64, opt_timefmt, localtime(&timestamp));
  out += printf("%s", buffer);

  return out;
}

static int print_filelist(alpm_filelist_t *filelist)
{
  int out = 0;
  size_t i;

  for(i = 0; i < filelist->count; i++) {
    out += printf("%s", (filelist->files + i)->name);
    out += print_escaped(opt_listdelim);
  }

  return out;
}

static bool backup_file_is_modified(const alpm_backup_t *backup_file)
{
  char fullpath[PATH_MAX];
  _cleanup_free_ char *md5sum = NULL;
  bool modified;

  /* TODO: respect expac->dbroot */
  snprintf(fullpath, sizeof(fullpath), "/%s", backup_file->name);

  md5sum = alpm_compute_md5sum(fullpath);
  if(md5sum == NULL) {
    return false;
  }

  modified = strcmp(md5sum, backup_file->hash) != 0;

  return modified;
}

static alpm_list_t *get_modified_files(alpm_pkg_t *pkg)
{
  alpm_list_t *i, *modified_files = NULL;

  for(i = alpm_pkg_get_backup(pkg); i; i = i->next) {
    const alpm_backup_t *backup = i->data;
    if(backup->hash && backup_file_is_modified(backup)) {
      modified_files = alpm_list_add(modified_files, backup->name);
    }
  }

  return modified_files;
}

static alpm_list_t *get_validation_method(alpm_pkg_t *pkg)
{
  alpm_list_t *validation = NULL;

  alpm_pkgvalidation_t v = alpm_pkg_get_validation(pkg);

  if(v == ALPM_PKG_VALIDATION_UNKNOWN) {
    return alpm_list_add(validation, "Unknown");
  }

  if(v & ALPM_PKG_VALIDATION_NONE) {
    return alpm_list_add(validation, "None");
  }

  if(v & ALPM_PKG_VALIDATION_MD5SUM) {
    validation = alpm_list_add(validation, "MD5 Sum");
  }
  if(v & ALPM_PKG_VALIDATION_SHA256SUM) {
    validation = alpm_list_add(validation, "SHA256 Sum");
  }
  if(v & ALPM_PKG_VALIDATION_SIGNATURE) {
    validation = alpm_list_add(validation, "Signature");
  }

  return validation;
}

static void print_pkg(alpm_pkg_t *pkg, const char *format)
{
  const char *f, *end;
  int out = 0;

  end = format + strlen(format);

  for(f = format; f < end; f++) {
    if(*f == '%') {
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
          out += printf(fmt, alpm_pkg_get_reason(pkg) ? "dependency" : "explicit");
          break;
        case '!': /* result number */
          fmt[strlen(fmt)-1] = 'd';
          out += printf(fmt, opt_pkgcounter++);
          break;
        case 'g': /* base64 gpg sig */
          out += printf(fmt, alpm_pkg_get_base64_sig(pkg));
          break;
        case 'h': /* sha256sum */
          out += printf(fmt, alpm_pkg_get_sha256sum(pkg));
          break;

        /* times */
        case 'b': /* build date */
          out += print_time(alpm_pkg_get_builddate(pkg));
          break;
        case 'l': /* install date */
          out += print_time(alpm_pkg_get_installdate(pkg));
          break;

        /* sizes */
        case 'k': /* download size */
          out += printf(fmt, size_to_string(alpm_pkg_get_size(pkg)));
          break;
        case 'm': /* install size */
          out += printf(fmt, size_to_string(alpm_pkg_get_isize(pkg)));
          break;

        /* lists */
        case 'F': /* files */
          out += print_filelist(alpm_pkg_get_files(pkg));
          break;
        case 'N': /* requiredby */
          out += print_list(alpm_pkg_compute_requiredby(pkg), NULL);
          break;
        case 'L': /* licenses */
          out += print_list(alpm_pkg_get_licenses(pkg), NULL);
          break;
        case 'G': /* groups */
          out += print_list(alpm_pkg_get_groups(pkg), NULL);
          break;
        case 'E': /* depends (shortdeps) */
          out += print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_get_name);
          break;
        case 'D': /* depends */
          out += print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_compute_string);
          break;
        case 'O': /* optdepends */
          out += print_list(alpm_pkg_get_optdepends(pkg), (extractfn)format_optdep);
          break;
        case 'o': /* optdepends (shortdeps) */
          out += print_list(alpm_pkg_get_optdepends(pkg), (extractfn)alpm_dep_get_name);
          break;
        case 'C': /* conflicts */
          out += print_list(alpm_pkg_get_conflicts(pkg), (extractfn)alpm_dep_get_name);
          break;
        case 'S': /* provides (shortdeps) */
          out += print_list(alpm_pkg_get_provides(pkg), (extractfn)alpm_dep_get_name);
          break;
        case 'P': /* provides */
          out += print_list(alpm_pkg_get_provides(pkg), (extractfn)alpm_dep_compute_string);
          break;
        case 'R': /* replaces */
          out += print_list(alpm_pkg_get_replaces(pkg), (extractfn)alpm_dep_get_name);
          break;
        case 'B': /* backup */
          out += print_list(alpm_pkg_get_backup(pkg), (extractfn)alpm_backup_get_name);
          break;
        case 'V': /* package validation */
          out += print_allocated_list(get_validation_method(pkg), NULL);
          break;
        case 'M': /* modified */
          out += print_allocated_list(get_modified_files(pkg), NULL);
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
    } else if(*f == '\\') {
      char esc[3] = { f[0], f[1], '\0' };
      out += print_escaped(esc);
      ++f;
    } else {
      fputc(*f, stdout);
      out++;
    }
  }

  /* only print a delimeter if any package data was outputted */
  if(out > 0) {
    print_escaped(opt_delim);
  }
}

static alpm_list_t *all_packages(alpm_list_t *dbs)
{
  alpm_list_t *i, *packages = NULL;

  for(i = dbs; i; i = i->next) {
    packages = alpm_list_join(packages, alpm_list_copy(alpm_db_get_pkgcache(i->data)));
  }

  return packages;
}

static alpm_list_t *search_packages(alpm_list_t *dbs, alpm_list_t *targets)
{
  alpm_list_t *i, *packages = NULL;

  for(i = dbs; i; i = i->next) {
    packages = alpm_list_join(packages, alpm_db_search(i->data, targets));
  }

  return packages;
}

static alpm_list_t *search_groups(alpm_list_t *dbs, alpm_list_t *groupnames)
{
  alpm_list_t *i, *j, *packages = NULL;

  for(i = groupnames; i; i = i->next) {
    for(j = dbs; j; j = j->next) {
      alpm_group_t *grp = alpm_db_get_group(j->data, i->data);
      if(grp != NULL) {
        packages = alpm_list_join(packages, alpm_list_copy(grp->packages));
      }
    }
  }

  return packages;
}

static alpm_list_t *search_exact(alpm_list_t *dblist, alpm_list_t *targets)
{
  char *pkgname, *reponame;
  alpm_list_t *results = NULL;

  /* resolve each target individually from the repo pool */
  for(alpm_list_t *t = targets; t; t = t->next) {
    alpm_pkg_t *pkg = NULL;
    alpm_list_t *r;
    int found = 0;

    pkgname = reponame = t->data;
    if(strchr(pkgname, '/')) {
      strsep(&pkgname, "/");
    } else {
      reponame = NULL;
    }

    for(r = dblist; r; r = r->next) {
      alpm_db_t *repo = r->data;

      if(reponame && strcmp(reponame, alpm_db_get_name(repo)) != 0) {
        continue;
      }

      pkg = alpm_db_get_pkg(repo, pkgname);
      if(pkg == NULL) {
        continue;
      }

      found = 1;
      results = alpm_list_add(results, pkg);
      if(opt_readone) {
        break;
      }
    }

    if(!found && opt_verbose) {
      fprintf(stderr, "error: package `%s' not found\n", pkgname);
    }
  }

  return results;
}

static alpm_list_t *resolve_targets(alpm_list_t *dblist, alpm_list_t *targets)
{
  if(targets == NULL) {
    return all_packages(dblist);
  }

  if(opt_what == SEARCH_REGEX) {
    return search_packages(dblist, targets);
  }

  if(opt_what == SEARCH_GROUPS) {
    return search_groups(dblist, targets);
  }

  return search_exact(dblist, targets);
}

static void expac_free(expac_t *expac)
{
  if(expac == NULL) {
    return;
  }

  alpm_release(expac->alpm);
}

static int expac_new(expac_t **expac, const char *config_file)
{
  expac_t *e;
  enum _alpm_errno_t alpm_errno = 0;
  config_t config;
  const char *dbroot = "/";
  const char *dbpath = "/var/lib/pacman";
  int r;

  e = calloc(1, sizeof(*e));
  if(e == NULL) {
    return -ENOMEM;
  }

  memset(&config, 0, sizeof(config));

  r = config_parse(&config, config_file);
  if(r < 0) {
    return r;
  }

  if(config.dbpath) {
    dbpath = config.dbpath;
  }

  if(config.dbroot) {
    dbroot = config.dbroot;
  }

  e->alpm = alpm_initialize(dbroot, dbpath, &alpm_errno);
  if(!e->alpm) {
    return -alpm_errno;
  }

  for(int i = 0; i < config.size; ++i) {
    alpm_register_syncdb(e->alpm, config.repos[i], 0);
  }

  config_reset(&config);

  *expac = e;

  return 0;
}

static alpm_list_t *expac_search_files(expac_t *expac, alpm_list_t *targets)
{
  alpm_list_t *i, *r = NULL;

  for(i = targets; i; i = i->next) {
    const char *path = i->data;
    alpm_pkg_t *pkg;

    if(alpm_pkg_load(expac->alpm, path, 0, 0, &pkg) != 0) {
      fprintf(stderr, "error: %s: %s\n", path,
          alpm_strerror(alpm_errno(expac->alpm)));
      continue;
    }

    r = alpm_list_add(r, pkg);
  }

  return r;
}

static alpm_list_t *expac_search_local(expac_t *expac, alpm_list_t *targets)
{
  alpm_list_t *dblist, *r;

  dblist = alpm_list_add(NULL, alpm_get_localdb(expac->alpm));
  r = resolve_targets(dblist, targets);
  alpm_list_free(dblist);

  return r;
}

static alpm_list_t *expac_search_sync(expac_t *expac, alpm_list_t *targets)
{
  return resolve_targets(alpm_get_syncdbs(expac->alpm), targets);
}

static alpm_list_t *expac_search(expac_t *expac, package_corpus_t corpus, alpm_list_t *targets)
{
  switch (corpus) {
  case CORPUS_LOCAL:
    return expac_search_local(expac, targets);
  case CORPUS_SYNC:
    return expac_search_sync(expac, targets);
  case CORPUS_FILE:
    return expac_search_files(expac, targets);
  }

  /* should be unreachable */
  return NULL;
}

static int read_targets_from_file(FILE *in, alpm_list_t **targets)
{
  char line[BUFSIZ];
  int i = 0, end = 0;
  while(!end) {
    line[i] = fgetc(in);

    if(line[i] == EOF)
      end = 1;

    if(isspace(line[i]) || end) {
      line[i] = '\0';
      /* avoid adding zero length arg, if multiple spaces separate args */
      if(i > 0) {
        if(!alpm_list_find_str(*targets, line))
          *targets = alpm_list_add(*targets, strdup(line));
        i = 0;
      }
    } else {
      ++i;
      if(i >= BUFSIZ) {
        fprintf(stderr, "error: buffer overflow on stdin\n");
        return -1;
      }
    }
  }

  return 0;
}

static alpm_list_t *process_targets(int argc, char **argv)
{
  alpm_list_t *r = NULL;
  int allow_stdin;

  allow_stdin = !isatty(STDIN_FILENO);

  for(int i = 0; i < argc; ++i) {
    if(allow_stdin && strcmp(argv[i], "-") == 0) {
      int k;

      k = read_targets_from_file(stdin, &r);
      if(k < 0) {
        return NULL;
      }

      allow_stdin = 0;
    } else {
      r = alpm_list_add(r, strdup(argv[i]));
    }
  }

  return r;
}

int main(int argc, char *argv[])
{
  alpm_list_t *results = NULL, *targets = NULL;
  expac_t *expac = NULL;
  int r;

  r = parse_options(&argc, &argv);
  if(r < 0) {
    return 1;
  }

  targets = process_targets(argc, argv);

  r = expac_new(&expac, opt_config_file);
  if(r < 0) {
    return 1;
  }

  results = expac_search(expac, opt_corpus, targets);
  if(results == NULL) {
    return 1;
  }

  for(alpm_list_t *i = results; i; i = i->next) {
    print_pkg(i->data, opt_format);
  }

  alpm_list_free_inner(targets, free);
  alpm_list_free(targets);
  alpm_list_free(results);
  expac_free(expac);

  return 0;
}

/* vim: set et ts=2 sw=2: */
