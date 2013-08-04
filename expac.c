/* Copyright (c) 2010-2011 Dave Reisner
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

#define _GNU_SOURCE
#include <alpm.h>
#include <ctype.h>
#include <getopt.h>
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
bool readone = false;
bool verbose = false;
bool search = false;
bool local = false;
bool groups = false;
bool localpkg = false;
char humansize = 'B';
const char *format = NULL;
const char *timefmt = NULL;
const char *listdelim = NULL;
const char *delim = NULL;
int pkgcounter = 0;

typedef const char *(*extractfn)(void*);

static const char *alpm_backup_get_name(void *b)
{
  alpm_backup_t *bkup = b;
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

static char *size_to_string(off_t pkgsize)
{
  static char out[64];

  if(humansize == 'B') {
    snprintf(out, sizeof(out), "%jd", (intmax_t)pkgsize);
  } else {
    snprintf(out, sizeof(out), "%.2f %ciB", humanize_size(pkgsize, humansize, NULL), humansize);
  }

  return out;
}

static char *strtrim(char *str) {
  char *pch = str;

  if (!str || *str == '\0') {
    return str;
  }

  while (isspace((unsigned char)*pch)) {
    pch++;
  }
  if (pch != str) {
    memmove(str, pch, (strlen(pch) + 1));
  }

  if (*str == '\0') {
    return str;
  }

  pch = (str + (strlen(str) - 1));
  while (isspace((unsigned char)*pch)) {
    pch--;
  }
  *++pch = '\0';

  return str;
}

static char *format_optdep(alpm_depend_t *optdep) {
  char *out;

  if (asprintf(&out, "%s: %s", optdep->name, optdep->desc) < 0) {
    return NULL;
  }

  return out;
}

static alpm_handle_t *alpm_init(void) {
  alpm_handle_t *handle = NULL;
  enum _alpm_errno_t alpm_errno = 0;
  FILE *fp;
  char line[PATH_MAX];
  char *ptr, *section = NULL;

  handle = alpm_initialize("/", "/var/lib/pacman", &alpm_errno);
  if (!handle) {
    alpm_strerror(alpm_errno);
    return NULL;
  }

  db_local = alpm_get_localdb(handle);

  fp = fopen("/etc/pacman.conf", "r");
  if (!fp) {
    perror("fopen: /etc/pacman.conf");
    return handle;
  }

  while (fgets(line, PATH_MAX, fp)) {
    strtrim(line);

    if (strlen(line) == 0 || line[0] == '#') {
      continue;
    }
    if ((ptr = strchr(line, '#'))) {
      *ptr = '\0';
    }

    if (line[0] == '[' && line[strlen(line) - 1] == ']') {
      ptr = &line[1];
      if (section) {
        free(section);
      }

      section = strdup(ptr);
      section[strlen(section) - 1] = '\0';

      if (strcmp(section, "options") != 0) {
        alpm_register_syncdb(handle, section,
            ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL);
      }
    }
  }

  free(section);
  fclose(fp);
  return handle;
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

static int parse_options(int argc, char *argv[], alpm_handle_t *handle) {
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
        dblist = alpm_list_copy(alpm_get_syncdbs(handle));
        break;
      case 'Q':
        if (dblist) {
          fprintf(stderr, "error: can only select one repo option (use -h for help)\n");
          return 1;
        }
        dblist = alpm_list_add(dblist, db_local);
        local = true;
        break;
      case '1':
        readone = true;
        break;
      case 'd':
        delim = optarg;
        break;
      case 'g':
        groups = true;
        break;
      case 'l':
        listdelim = optarg;
        break;
      case 'H':
        for(i = SIZE_TOKENS; *i; i++) {
          if(*i == *optarg) {
            humansize = *optarg;
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
        localpkg = true;
        break;
      case 's':
        search = true;
        break;
      case 't':
        timefmt = optarg;
        break;
      case 'v':
        verbose = true;
        break;

      case '?':
        return 1;
      default:
        return 1;
    }
  }

  if (optind < argc) {
    format = argv[optind++];
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

static int print_list(alpm_list_t *list, extractfn fn, bool shortdeps) {
  alpm_list_t *i;
  int out = 0;

  if (!list) {
    if (verbose) {
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

    if (shortdeps) {
      *(item + strcspn(item, "<>=")) = '\0';
    }

    out += printf("%s", item);

    if ((i = alpm_list_next(i))) {
      out += print_escaped(listdelim);
    } else {
      break;
    }
  }

  return out;
}

static int print_time(time_t timestamp) {
  char buffer[64];
  int out = 0;

  if (!timestamp) {
    if (verbose) {
      out += printf("None");
    }
    return out;
  }

  /* no overflow here, strftime prints a max of 64 including null */
  strftime(&buffer[0], 64, timefmt, localtime(&timestamp));
  out += printf("%s", buffer);

  return out;
}

static int print_filelist(alpm_filelist_t *filelist) {
  int out = 0;
  size_t i;

  for (i = 0; i < filelist->count; i++) {
    out += printf("%s", (filelist->files + i)->name);
    out += print_escaped(listdelim);
  }

  return out;
}

static bool backup_file_is_modified(const alpm_backup_t *backup_file) {
  char fullpath[PATH_MAX];
  char *md5sum;
  bool modified;

  snprintf(fullpath, PATH_MAX, "/%s", backup_file->name);

  if(access(fullpath, R_OK) != 0) {
    return false;
  }

  md5sum = alpm_compute_md5sum(fullpath);
  if(md5sum == NULL) {
    return false;
  }

  modified = strcmp(md5sum, backup_file->hash) != 0;

  free(md5sum);

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

  end = rawmemchr(format, '\0');

  for (f = format; f < end; f++) {
    bool shortdeps = false;
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
          snprintf(buf, sizeof(buf), "%d", pkgcounter++);
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
          out += print_list(alpm_pkg_compute_requiredby(pkg), NULL, shortdeps);
          break;
        case 'L': /* licenses */
          out += print_list(alpm_pkg_get_licenses(pkg), NULL, shortdeps);
          break;
        case 'G': /* groups */
          out += print_list(alpm_pkg_get_groups(pkg), NULL, shortdeps);
          break;
        case 'E': /* depends (shortdeps) */
          out += print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_get_name, shortdeps);
          break;
        case 'D': /* depends */
          out += print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_compute_string, shortdeps);
          break;
        case 'O': /* optdepends */
          out += print_list(alpm_pkg_get_optdepends(pkg), (extractfn)format_optdep, shortdeps);
          break;
        case 'o': /* optdepends (shortdeps) */
          out += print_list(alpm_pkg_get_optdepends(pkg), (extractfn)alpm_dep_get_name, shortdeps);
          break;
        case 'C': /* conflicts */
          out += print_list(alpm_pkg_get_conflicts(pkg), (extractfn)alpm_dep_get_name, shortdeps);
          break;
        case 'S': /* provides (shortdeps) */
          out += print_list(alpm_pkg_get_provides(pkg), (extractfn)alpm_dep_get_name, shortdeps);
          break;
        case 'P': /* provides */
          out += print_list(alpm_pkg_get_provides(pkg), (extractfn)alpm_dep_compute_string, shortdeps);
          break;
        case 'R': /* replaces */
          out += print_list(alpm_pkg_get_replaces(pkg), (extractfn)alpm_dep_get_name, shortdeps);
          break;
        case 'B': /* backup */
          out += print_list(alpm_pkg_get_backup(pkg), alpm_backup_get_name, shortdeps);
          break;
        case 'V': /* package validation */
          out += print_list(get_validation_method(pkg), NULL, false);
          break;
        case 'M': /* modified */
        {
          alpm_list_t *modified_files = get_modified_files(pkg);
          if(modified_files != NULL) {
            out += print_list(modified_files, NULL, shortdeps);
            alpm_list_free(modified_files);
          }
          break;
        }
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
      char buf[3]; /* its not safe to do this in a single sprintf */
      buf[0] = *f;
      buf[1] = *++f;
      buf[2] = '\0';
      out += print_escaped(buf);
    } else {
      fputc(*f, stdout);
      out++;
    }
  }

  /* only print a delimeter if any package data was outputted */
  if (out > 0) {
    print_escaped(delim);
  }

  return !out;
}

static alpm_list_t *resolve_pkg(alpm_list_t *targets) {
  char *pkgname, *reponame;
  alpm_list_t *t, *r, *ret = NULL;

  if (!targets) {
    for (r = dblist; r; r = alpm_list_next(r)) {
      /* joining causes corruption on alpm_release(), so we copy */
      ret = alpm_list_join(ret, alpm_list_copy(alpm_db_get_pkgcache(r->data)));
    }
  } else if (search) {
    for (r = dblist; r; r = alpm_list_next(r)) {
      ret = alpm_list_join(ret, alpm_db_search(r->data, targets));
    }
  } else if (groups) {
    for (t = targets; t; t = alpm_list_next(t)) {
      for (r = dblist; r; r = alpm_list_next(r)) {
        alpm_group_t *grp = alpm_db_get_group(r->data, t->data);
        if (grp) {
          ret = alpm_list_join(ret, alpm_list_copy(grp->packages));
        }
      }
    }
  } else {
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

        if (!(pkg = alpm_db_get_pkg(repo, pkgname)) &&
            !(pkg = alpm_find_satisfier(alpm_db_get_pkgcache(repo), pkgname))) {
          continue;
        }

        found = 1;
        ret = alpm_list_add(ret, pkg);
        if (readone) {
          break;
        }
      }
      if (!found && verbose) {
        fprintf(stderr, "error: package `%s' not found\n", pkgname);
      }
    }
  }

  return ret;
}

int main(int argc, char *argv[]) {
  int ret = 1;
  alpm_handle_t *handle;
  alpm_list_t *results = NULL, *i;

  handle = alpm_init();
  if (!handle) {
    return ret;
  }

  ret = parse_options(argc, argv, handle);
  if (ret != 0) {
    goto finish;
  }

  /* ensure sane defaults */
  if (!dblist && !localpkg) {
    local = true;
    dblist = alpm_list_add(dblist, db_local);
  }

  delim = delim ? delim : DEFAULT_DELIM;
  listdelim = listdelim ? listdelim : DEFAULT_LISTDELIM;
  timefmt = timefmt ? timefmt : DEFAULT_TIMEFMT;

  if (localpkg) {
    /* load each target as a package */
    for (i = targets; i; i = alpm_list_next(i)) {
      alpm_pkg_t *pkg;
      int err;

      err = alpm_pkg_load(handle, i->data, 0,
          ALPM_SIG_PACKAGE|ALPM_SIG_PACKAGE_OPTIONAL, &pkg);
      if (err) {
        fprintf(stderr, "error: %s: %s\n", (const char*)i->data,
            alpm_strerror(alpm_errno(handle)));
        continue;
      }
      results = alpm_list_add(results, pkg);
    }
  } else {
    results = resolve_pkg(targets);
    if (!results) {
      ret = 1;
      goto finish;
    }
  }

  for (i = results; i; i = alpm_list_next(i)) {
    alpm_pkg_t *pkg = i->data;
    ret += print_pkg(pkg, format);
  }
  ret = !!ret; /* clamp to zero/one */

  if(localpkg) {
    alpm_list_free_inner(results, (alpm_list_fn_free)alpm_pkg_free);
  }
  alpm_list_free(results);

finish:
  alpm_list_free(dblist);
  alpm_list_free(targets);
  alpm_release(handle);
  return ret;
}

/* vim: set et ts=2 sw=2: */
