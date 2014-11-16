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

#define DEFAULT_DELIM        "\n"
#define DEFAULT_LISTDELIM    "  "
#define DEFAULT_TIMEFMT      "%c"
#define FORMAT_TOKENS        "BCDEGLMNOPRSVabdhmnprsuvw%"
#define SIZE_TOKENS          "BKMGTPEZY\0"

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif

static char const digits[] = "0123456789";
static char const printf_flags[] = "'-+ #0I";

alpm_db_t *db_local = NULL;
alpm_list_t *dblist = NULL;
alpm_list_t *targets = NULL;
bool opt_readone = false;
bool opt_verbose = false;
bool opt_search = false;
bool opt_local = false;
bool opt_groups = false;
bool opt_localpkg = false;
char opt_humansize = 'B';
const char *opt_format = NULL;
const char *opt_timefmt = DEFAULT_TIMEFMT;
const char *opt_listdelim = DEFAULT_LISTDELIM;
const char *opt_delim = DEFAULT_DELIM;
int opt_pkgcounter = 0;

typedef const char *(*extractfn)(void*);

typedef struct config_t {
  char **repos;
  int size;
  int capacity;
} config_t;

static inline void freep(void *p) { free(*(void **)p); }
static inline void fclosep(FILE **p) { if (*p) fclose(*p); }
static inline void globfreep(glob_t *p) { globfree(p); }
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)

static size_t strtrim(char *str) {
  char *left = str, *right;

  if (!str || *str == '\0') {
    return 0;
  }

  while (isspace((unsigned char)*left)) {
    left++;
  }
  if (left != str) {
    memmove(str, left, (strlen(left) + 1));
    left = str;
  }

  if (*str == '\0') {
    return 0;
  }

  right = strchr(str, '\0') - 1;
  while (isspace((unsigned char)*right)) {
    right--;
  }
  *++right = '\0';

  return right - left;
}

int is_section(const char *s, int n) {
  return s[0] == '[' && s[n-1] == ']';
}

static int config_add_repo(config_t *config, char *reponame) {
  /* first time setup */
  if (config->repos == NULL) {
    config->repos = calloc(10, sizeof(char*));
    if (config->repos == NULL) {
      return -ENOMEM;
    }

    config->size = 0;
    config->capacity = 10;
  }

  /* grow when needed */
  if (config->size == config->capacity) {
    void *ptr;

    ptr = realloc(config->repos, config->capacity * 2.5 * sizeof(char*));
    if (ptr == NULL) {
      return -ENOMEM;
    }

    config->repos = ptr;
  }

  config->repos[config->size] = strdup(reponame);
  ++config->size;

  return 0;
}

void config_reset(config_t *config) {
  if (config == NULL)
    return;

  for (int i = 0; i < config->size; ++i)
    free(config->repos[i]);

  free(config->repos);
}

static int parse_one_file(config_t *config, const char *filename, char **section);

static int parse_include(config_t *config, const char *include, char **section) {
  _cleanup_(globfreep) glob_t globbuf = {};

  if (glob(include, GLOB_NOCHECK, NULL, &globbuf) != 0) {
    fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
            include);
    return -ENOMEM;
  }

  for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
    int r;
    r = parse_one_file(config, globbuf.gl_pathv[i], section);
    if (r < 0)
      return r;
  }

  return 0;
}

static char *split_keyval(char *line, const char *sep) {
  strsep(&line, sep);
  return line;
}

static int parse_one_file(config_t *config, const char *filename, char **section) {
  _cleanup_(fclosep) FILE *fp = NULL;
  _cleanup_free_ char *line = NULL;
  size_t n = 0;
  int in_options = 0;

  if (*section)
    in_options = strcmp(*section, "options") == 0;

  fp = fopen(filename, "r");
  if (fp == NULL)
    return -errno;

  for (;;) {
    ssize_t len;

    errno = 0;
    len = getline(&line, &n, fp);
    if (len < 0) {
      if (errno != 0)
        return -errno;

      /* EOF */
      break;
    }

    len = strtrim(line);
    if (len == 0 || line[0] == '#')
      continue;

    if (is_section(line, len)) {
      free(*section);
      *section = strndup(&line[1], len - 2);
      if (*section == NULL)
        return -ENOMEM;

      in_options = strcmp(*section, "options") == 0;
      if (!in_options) {
        int r;

        r = config_add_repo(config, *section);
        if (r < 0)
          return r;

      }
      continue;
    }

    if (in_options && memchr(line, '=', len)) {
      char *val;

      val = split_keyval(line, "=");
      strtrim(line);

      if (strcmp(line, "Include") == 0) {
        int k;

        strtrim(val);

        k = parse_include(config, val, section);
        if (k < 0)
          return k;
      }
    }
  }

  return 0;
}

static int config_parse(config_t *config, const char *filename) {
  _cleanup_free_ char *section = NULL;

  return parse_one_file(config, filename, &section);
}

static const char *alpm_backup_get_name(alpm_backup_t *bkup) {
  return bkup->name;
}

static double humanize_size(off_t bytes, const char target_unit, const char **label)
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

static char *size_to_string(off_t pkgsize) {
  static char out[64];

  if(opt_humansize == 'B') {
    snprintf(out, sizeof(out), "%jd", (intmax_t)pkgsize);
  } else {
    snprintf(out, sizeof(out), "%.2f %ciB", humanize_size(pkgsize, opt_humansize, NULL), opt_humansize);
  }

  return out;
}

static char *format_optdep(alpm_depend_t *optdep) {
  char *out;

  if (asprintf(&out, "%s: %s", optdep->name, optdep->desc) < 0) {
    return NULL;
  }

  return out;
}

static alpm_handle_t *alpm_init(void) {
  alpm_handle_t *alpm = NULL;
  enum _alpm_errno_t alpm_errno = 0;
  config_t config = { NULL, 0, 0 };
  int r;

  alpm = alpm_initialize("/", "/var/lib/pacman", &alpm_errno);
  if (!alpm) {
    alpm_strerror(alpm_errno);
    return NULL;
  }

  db_local = alpm_get_localdb(alpm);

  r = config_parse(&config, "/etc/pacman.conf");
  if (r < 0) {
    fprintf(stderr, "error: failed to parse config: %s\n", strerror(-r));
    return NULL;
  }

  for (int i = 0; i < config.size; ++i) {
    alpm_register_syncdb(alpm, config.repos[i], 0);
  }

  config_reset(&config);

  return alpm;
}

static const char *alpm_dep_get_name(void *dep) {
  return ((alpm_depend_t*)dep)->name;
}

static void usage(void) {
  fprintf(stderr, "expac %s\n"
      "Usage: expac [options] <format> target...\n\n", VERSION);
  fprintf(stderr,
      " Options:\n"
      "  -Q, --local               search local DB (default)\n"
      "  -S, --sync                search sync DBs\n"
      "  -s, --search              search for matching regex\n"
      "  -g, --group               return packages matching targets as groups\n"
      "  -H, --humansize <size>    format package sizes in SI units (default: bytes)\n"
      "  -1, --readone             return only the first result of a sync search\n\n"
      "  -d, --delim <string>      separator used between packages (default: \"\\n\")\n"
      "  -l, --listdelim <string>  separator used between list elements (default: \"  \")\n"
      "  -p, --file                query local files instead of the DB\n"
      "  -t, --timefmt <fmt>       date format passed to strftime (default: \"%%c\")\n\n"
      "  -v, --verbose             be more verbose\n\n"
      "  -h, --help                display this help and exit\n\n"
      "For more details see expac(1).\n");
}

static int parse_options(int argc, char *argv[], alpm_handle_t *alpm) {
  int opt, option_index = 0;
  const char *i;

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
    {0, 0, 0, 0}
  };

  while (-1 != (opt = getopt_long(argc, argv, "1l:d:gH:hf:pQSst:v", opts, &option_index))) {
    switch (opt) {
      case 'S':
        if (dblist) {
          fprintf(stderr, "error: can only select one repo option (use -h for help)\n");
          return 1;
        }
        dblist = alpm_list_copy(alpm_get_syncdbs(alpm));
        break;
      case 'Q':
        if (dblist) {
          fprintf(stderr, "error: can only select one repo option (use -h for help)\n");
          return 1;
        }
        dblist = alpm_list_add(dblist, db_local);
        opt_local = true;
        break;
      case '1':
        opt_readone = true;
        break;
      case 'd':
        opt_delim = optarg;
        break;
      case 'g':
        opt_groups = true;
        break;
      case 'l':
        opt_listdelim = optarg;
        break;
      case 'H':
        for(i = SIZE_TOKENS; *i; i++) {
          if(*i == *optarg) {
            opt_humansize = *optarg;
            break;
          }
        }
        if(*i == '\0') {
          fprintf(stderr, "error: invalid SI size formatter: %c\n", *optarg);
          return 1;
        }
        break;
      case 'h':
        usage();
        return 1;
      case 'p':
        opt_localpkg = true;
        break;
      case 's':
        opt_search = true;
        break;
      case 't':
        opt_timefmt = optarg;
        break;
      case 'v':
        opt_verbose = true;
        break;

      case '?':
        return 1;
      default:
        return 1;
    }
  }

  if (optind < argc) {
    opt_format = argv[optind++];
  } else {
    fprintf(stderr, "error: missing format string (use -h for help)\n");
    return 1;
  }

  while (optind < argc) {
    targets = alpm_list_add(targets, argv[optind++]);
  }

  return 0;
}

static int print_escaped(const char *delim) {
  const char *f;
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
        ++out;
      }
    } else {
      fputc(*f, stdout);
      ++out;
    }
  }

  return out;
}

static int print_list(alpm_list_t *list, extractfn fn) {
  alpm_list_t *i;
  int out = 0;

  if (!list) {
    if (opt_verbose) {
      out += printf("None");
    }
    return out;
  }

  i = list;
  while (1) {
    char *item = (char*)(fn ? fn(i->data) : i->data);
    if (item == NULL) {
      continue;
    }

    out += printf("%s", item);

    if ((i = alpm_list_next(i))) {
      out += print_escaped(opt_listdelim);
    } else {
      break;
    }
  }

  return out;
}

static int print_allocated_list(alpm_list_t *list, extractfn fn) {
  int out = print_list(list, fn);
  alpm_list_free(list);
  return out;
}

static int print_time(time_t timestamp) {
  char buffer[64];
  int out = 0;

  if (!timestamp) {
    if (opt_verbose) {
      out += printf("None");
    }
    return out;
  }

  /* no overflow here, strftime prints a max of 64 including null */
  strftime(&buffer[0], 64, opt_timefmt, localtime(&timestamp));
  out += printf("%s", buffer);

  return out;
}

static int print_filelist(alpm_filelist_t *filelist) {
  int out = 0;
  size_t i;

  for (i = 0; i < filelist->count; i++) {
    out += printf("%s", (filelist->files + i)->name);
    out += print_escaped(opt_listdelim);
  }

  return out;
}

static bool backup_file_is_modified(const alpm_backup_t *backup_file) {
  char fullpath[PATH_MAX];
  _cleanup_free_ char *md5sum = NULL;
  bool modified;

  snprintf(fullpath, sizeof(fullpath), "/%s", backup_file->name);

  md5sum = alpm_compute_md5sum(fullpath);
  if(md5sum == NULL) {
    return false;
  }

  modified = strcmp(md5sum, backup_file->hash) != 0;

  return modified;
}

static alpm_list_t *get_modified_files(alpm_pkg_t *pkg) {
  alpm_list_t *i, *modified_files = NULL;

  for(i = alpm_pkg_get_backup(pkg); i; i = alpm_list_next(i)) {
    const alpm_backup_t *backup = i->data;
    if(backup->hash && backup_file_is_modified(backup)) {
      modified_files = alpm_list_add(modified_files, backup->name);
    }
  }

  return modified_files;
}

static alpm_list_t *get_validation_method(alpm_pkg_t *pkg) {
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

static int print_pkg(alpm_pkg_t *pkg, const char *format) {
  const char *f, *end;
  char fmt[64], buf[64];
  int len, out = 0;

  end = format + strlen(format);

  for (f = format; f < end; f++) {
    len = 0;
    if (*f == '%') {
      len = strspn(f + 1 + len, printf_flags);
      len += strspn(f + 1 + len, digits);
      snprintf(fmt, len + 3, "%ss", f);
      fmt[len + 1] = 's';
      f += len + 1;
      switch (*f) {
        /* simple attributes */
        case 'f': /* filename */
          out += printf(fmt, alpm_pkg_get_filename(pkg));
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
        case 'a': /* architecutre */
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
          snprintf(buf, sizeof(buf), "%d", opt_pkgcounter++);
          out += printf(fmt, buf);
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
    } else if (*f == '\\') {
      char esc[3] = { f[0], f[1], '\0' };
      out += print_escaped(esc);
      ++f;
    } else {
      fputc(*f, stdout);
      out++;
    }
  }

  /* only print a delimeter if any package data was outputted */
  if (out > 0) {
    print_escaped(opt_delim);
  }

  return !out;
}

static alpm_list_t *all_packages(alpm_list_t *dbs) {
  alpm_list_t *i, *packages = NULL;

  for (i = dbs; i; i = i->next) {
    packages = alpm_list_join(packages, alpm_list_copy(alpm_db_get_pkgcache(i->data)));
  }

  return packages;
}

static alpm_list_t *search_packages(alpm_list_t *dbs, alpm_list_t *targets) {
  alpm_list_t *i, *packages = NULL;

  for (i = dbs; i; i = i->next) {
    packages = alpm_list_join(packages, alpm_db_search(i->data, targets));
  }

  return packages;
}

static alpm_list_t *search_groups(alpm_list_t *dbs, alpm_list_t *groupnames) {
  alpm_list_t *i, *j, *packages = NULL;

  for (i = groupnames; i; i = i->next) {
    for (j = dbs; j; j = j->next) {
      alpm_group_t *grp = alpm_db_get_group(j->data, i->data);
      if (grp != NULL) {
        packages = alpm_list_join(packages, alpm_list_copy(grp->packages));
      }
    }
  }

  return packages;
}

static alpm_list_t *resolve_pkg(alpm_list_t *targets) {
  char *pkgname, *reponame;
  alpm_list_t *t, *r, *ret = NULL;

  if (targets == NULL) {
    return all_packages(dblist);
  } else if (opt_search) {
    return search_packages(dblist, targets);
  } else if (opt_groups) {
    return search_groups(dblist, targets);
  }

  /* resolve each target individually from the repo pool */
  for (t = targets; t; t = alpm_list_next(t)) {
    alpm_pkg_t *pkg = NULL;
    int found = 0;

    pkgname = reponame = t->data;
    if (strchr(pkgname, '/')) {
      strsep(&pkgname, "/");
    } else {
      reponame = NULL;
    }

    for (r = dblist; r; r = alpm_list_next(r)) {
      alpm_db_t *repo = r->data;

      if (reponame && strcmp(reponame, alpm_db_get_name(repo)) != 0) {
        continue;
      }

      pkg = alpm_db_get_pkg(repo, pkgname);
      if (pkg == NULL) {
        continue;
      }

      found = 1;
      ret = alpm_list_add(ret, pkg);
      if (opt_readone) {
        break;
      }
    }

    if (!found && opt_verbose) {
      fprintf(stderr, "error: package `%s' not found\n", pkgname);
    }
  }

  return ret;
}

static alpm_list_t *gather_packages(alpm_handle_t *alpm, alpm_list_t *targets) {
  alpm_list_t *results = NULL;

  if (opt_localpkg) {
    alpm_list_t *i;

    /* load each target as a package */
    for (i = targets; i; i = alpm_list_next(i)) {
      alpm_pkg_t *pkg;
      int err;

      err = alpm_pkg_load(alpm, i->data, 0, 0, &pkg);
      if (err) {
        fprintf(stderr, "error: %s: %s\n", (const char*)i->data,
            alpm_strerror(alpm_errno(alpm)));
        continue;
      }
      results = alpm_list_add(results, pkg);
    }
  } else {
    results = resolve_pkg(targets);
  }

  return results;
}

int main(int argc, char *argv[]) {
  int ret = 1;
  alpm_handle_t *alpm;
  alpm_list_t *results = NULL, *i;

  alpm = alpm_init();
  if (!alpm) {
    return ret;
  }

  ret = parse_options(argc, argv, alpm);
  if (ret != 0) {
    goto finish;
  }

  /* ensure sane defaults */
  if (!dblist && !opt_localpkg) {
    opt_local = true;
    dblist = alpm_list_add(dblist, db_local);
  }

  results = gather_packages(alpm, targets);
  if (results == NULL) {
    ret = 1;
    goto finish;
  }

  for (i = results; i; i = alpm_list_next(i)) {
    alpm_pkg_t *pkg = i->data;
    ret += print_pkg(pkg, opt_format);
  }
  ret = !!ret; /* clamp to zero/one */

  if(opt_localpkg) {
    alpm_list_free_inner(results, (alpm_list_fn_free)alpm_pkg_free);
  }
  alpm_list_free(results);

finish:
  alpm_list_free(dblist);
  alpm_list_free(targets);
  alpm_release(alpm);
  return ret;
}

/* vim: set et ts=2 sw=2: */
