#ifndef _CONF_H
#define _CONF_H

typedef struct config_t {
  char **repos;
  int size;
  int capacity;
} config_t;

int config_parse(config_t *config, const char *filename);
void config_reset(config_t *config);

#endif  /* _CONF_H */

/* vim: set et ts=2 sw=2: */
