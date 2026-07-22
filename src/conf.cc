// SPDX-License-Identifier: MIT

#include "conf.hh"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <string>

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

// strtrim() operates in place on a raw buffer; run a std::string through it
// via a fresh copy so callers never have to think about the underlying
// buffer being resized out from under them.
static std::string Trim(std::string s) {
  s.resize(strtrim(s.data()));
  return s;
}

static bool is_section(const std::string& s) {
  return !s.empty() && s.front() == '[' && s.back() == ']';
}

static int config_add_repo(config_t* config, const std::string& reponame) {
  config->repos.push_back(reponame);

  return 0;
}

static int parse_one_file(config_t* config, const char* filename,
                          std::string* section);

static int parse_include(config_t* config, const std::string& include,
                         std::string* section) {
  glob_t globbuf;
  int r = 0;

  if (glob(include.c_str(), GLOB_NOCHECK, NULL, &globbuf) != 0) {
    fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
            include.c_str());
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
                          std::string* section) {
  std::ifstream fp(filename);
  if (!fp) {
    return -errno;
  }

  int in_options = !section->empty() && *section == "options";

  std::string line;
  while (std::getline(fp, line)) {
    line = Trim(std::move(line));
    if (line.empty() || line[0] == '#') {
      continue;
    }

    if (is_section(line)) {
      *section = line.substr(1, line.size() - 2);

      in_options = (*section == "options");
      if (!in_options) {
        int r = config_add_repo(config, *section);
        if (r < 0) {
          return r;
        }
      }
      continue;
    }

    size_t eq = line.find('=');
    std::string key = Trim(eq == std::string::npos ? line : line.substr(0, eq));
    std::string val =
        Trim(eq == std::string::npos ? std::string() : line.substr(eq + 1));

    if (key == "Include") {
      int k = parse_include(config, val, section);
      if (k < 0) {
        return k;
      }
    }

    if (in_options) {
      if (key == "DBPath") {
        config->dbpath = val;
      } else if (key == "RootDir") {
        config->dbroot = val;
      }
    }
  }

  return 0;
}

int config_parse(config_t* config, const char* filename) {
  std::string section;

  return parse_one_file(config, filename, &section);
}

// vim: set et ts=2 sw=2:
