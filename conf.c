#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <string.h>
#include <stdlib.h>

#include "conf.h"
#include "util.h"

static size_t strtrim(char *str)
{
  char *left = str, *right;

  if(!str || *str == '\0') {
    return 0;
  }

  while(isspace((unsigned char)*left)) {
    left++;
  }
  if(left != str) {
    memmove(str, left, (strlen(left) + 1));
    left = str;
  }

  if(*str == '\0') {
    return 0;
  }

  right = strchr(str, '\0') - 1;
  while(isspace((unsigned char)*right)) {
    right--;
  }
  *++right = '\0';

  return right - left;
}

static int is_section(const char *s, int n)
{
  return s[0] == '[' && s[n-1] == ']';
}

static int config_add_repo(config_t *config, char *reponame)
{
  /* first time setup */
  if(config->repos == NULL) {
    config->repos = calloc(10, sizeof(char*));
    if(config->repos == NULL) {
      return -ENOMEM;
    }

    config->size = 0;
    config->capacity = 10;
  }

  /* grow when needed */
  if(config->size == config->capacity) {
    void *ptr;

    ptr = realloc(config->repos, config->capacity * 2.5 * sizeof(char*));
    if(ptr == NULL) {
      return -ENOMEM;
    }

    config->repos = ptr;
  }

  config->repos[config->size] = strdup(reponame);
  ++config->size;

  return 0;
}

static int parse_one_file(config_t *config, const char *filename, char **section);

static int parse_include(config_t *config, const char *include, char **section) {
  glob_t globbuf;
  int r = 0;

  if(glob(include, GLOB_NOCHECK, NULL, &globbuf) != 0) {
    fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
            include);
    return -ENOMEM;
  }

  for(size_t i = 0; i < globbuf.gl_pathc; ++i) {
    r = parse_one_file(config, globbuf.gl_pathv[i], section);
    if(r < 0) {
      break;
    }
  }

  globfree(&globbuf);
  return r;
}

static int parse_one_file(config_t *config, const char *filename, char **section)
{
  _cleanup_(fclosep) FILE *fp = NULL;
  _cleanup_free_ char *line = NULL;
  size_t n = 0;
  int in_options = 0;

  if(*section) {
    in_options = strcmp(*section, "options") == 0;
  }

  fp = fopen(filename, "r");
  if(fp == NULL) {
    return -errno;
  }

  for(;;) {
    ssize_t len;

    errno = 0;
    len = getline(&line, &n, fp);
    if(len < 0) {
      if(errno != 0) {
        return -errno;
      }

      /* EOF */
      break;
    }

    len = strtrim(line);
    if(len == 0 || line[0] == '#') {
      continue;
    }

    if(is_section(line, len)) {
      free(*section);
      *section = strndup(&line[1], len - 2);
      if(*section == NULL) {
        return -ENOMEM;
      }

      in_options = strcmp(*section, "options") == 0;
      if(!in_options) {
        int r;

        r = config_add_repo(config, *section);
        if(r < 0) {
          return r;
        }

      }
      continue;
    }

    if(in_options && memchr(line, '=', len)) {
      char *val = line;

      strsep(&val, "=");
      strtrim(line);
      strtrim(val);

      if(strcmp(line, "Include") == 0) {
        int k;

        k = parse_include(config, val, section);
        if(k < 0) {
          return k;
        }
      } else if(strcmp(line, "DBPath") == 0) {
        config->dbpath = strdup(val);
        if(config->dbpath == NULL) {
          return -ENOMEM;
        }
      } else if(strcmp(line, "RootDir") == 0) {
        config->dbroot = strdup(val);
        if(config->dbpath == NULL) {
          return -ENOMEM;
        }
      }
    }
  }

  return 0;
}

void config_reset(config_t *config)
{
  if(config == NULL) {
    return;
  }

  for(int i = 0; i < config->size; ++i) {
    free(config->repos[i]);
  }

  free(config->dbroot);
  free(config->dbpath);
  free(config->repos);
}

int config_parse(config_t *config, const char *filename)
{
  _cleanup_free_ char *section = NULL;

  return parse_one_file(config, filename, &section);
}

/* vim: set et ts=2 sw=2: */
