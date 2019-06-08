#ifndef _EXPAC_H
#define _EXPAC_H

#include <alpm.h>

typedef enum package_corpus_t {
  CORPUS_LOCAL,
  CORPUS_SYNC,
  CORPUS_FILE,
} package_corpus_t;

typedef enum search_what_t {
  SEARCH_EXACT,
  SEARCH_GROUPS,
  SEARCH_REGEX,
} search_what_t;

typedef struct expac_t {
  alpm_handle_t *alpm;
} expac_t;

#endif  /* _EXPAC_H */

/* vim: set et ts=2 sw=2: */
