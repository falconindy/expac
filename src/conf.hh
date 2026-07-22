#ifndef _CONF_H
#define _CONF_H

#include <optional>
#include <string>
#include <vector>

typedef struct config_t {
  std::vector<std::string> repos;

  std::optional<std::string> dbroot;
  std::optional<std::string> dbpath;
} config_t;

int config_parse(config_t* config, const char* filename);

#endif /* _CONF_H */

/* vim: set et ts=2 sw=2: */
