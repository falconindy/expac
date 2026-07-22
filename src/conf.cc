#include "conf.hh"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>

#include "util.hh"

static size_t strtrim(char* str) {
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

static int is_section(const char* s, int n) {
  return s[0] == '[' && s[n - 1] == ']';
}

static int config_add_repo(config_t* config, char* reponame) {
  config->repos.emplace_back(reponame);

  return 0;
}

static int parse_one_file(config_t* config, const char* filename,
                          char** section);

static int parse_include(config_t* config, const char* include,
                         char** section) {
  glob_t globbuf;
  int r = 0;

  if (glob(include, GLOB_NOCHECK, NULL, &globbuf) != 0) {
    fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
            include);
    return -ENOMEM;
  }

  for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
    r = parse_one_file(config, globbuf.gl_pathv[i], section);
    if (r < 0) {
      break;
    }
  }

  globfree(&globbuf);
  return r;
}

static int parse_one_file(config_t* config, const char* filename,
                          char** section) {
  FILE* fp = NULL;
  char* line = NULL;
  size_t n = 0;
  int in_options;

  in_options = *section && strcmp(*section, "options") == 0;

  fp = fopen(filename, "r");
  if (fp == NULL) {
    return -errno;
  }
  auto fp_guard = MakeScopeGuard([&] { fclose(fp); });
  auto line_guard = MakeScopeGuard([&] { free(line); });

  for (;;) {
    ssize_t len;
    char* val;

    errno = 0;
    len = getline(&line, &n, fp);
    if (len < 0) {
      if (errno != 0) {
        return -errno;
      }

      /* EOF */
      break;
    }

    len = strtrim(line);
    if (len == 0 || line[0] == '#') {
      continue;
    }

    if (is_section(line, len)) {
      free(*section);
      *section = strndup(&line[1], len - 2);
      if (*section == NULL) {
        return -ENOMEM;
      }

      in_options = strcmp(*section, "options") == 0;
      if (!in_options) {
        int r;

        r = config_add_repo(config, *section);
        if (r < 0) {
          return r;
        }
      }
      continue;
    }

    val = line;
    strsep(&val, "=");
    strtrim(line);
    strtrim(val);

    if (strcmp(line, "Include") == 0) {
      int k;

      k = parse_include(config, val, section);
      if (k < 0) {
        return k;
      }
    }

    if (in_options) {
      if (strcmp(line, "DBPath") == 0) {
        config->dbpath = val;
      } else if (strcmp(line, "RootDir") == 0) {
        config->dbroot = val;
      }
    }
  }

  return 0;
}

int config_parse(config_t* config, const char* filename) {
  char* section = NULL;
  auto section_guard = MakeScopeGuard([&] { free(section); });

  return parse_one_file(config, filename, &section);
}

/* vim: set et ts=2 sw=2: */
