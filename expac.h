#ifndef _EXPAC_H
#define _EXPAC_H

#include <alpm.h>

typedef enum SearchCorpus {
  SEARCH_LOCAL,
  SEARCH_SYNC,
  SEARCH_FILE,
} SearchCorpus;

typedef struct Expac {
  alpm_handle_t *alpm;
  alpm_db_t *db_local;

  SearchCorpus search_type;
} Expac;

int expac_new(Expac **expac, int argc, char **argv);

#endif  /* _EXPAC_H */

/* vim: set et ts=2 sw=2: */
