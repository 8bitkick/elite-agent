// The AI viewer on the Presto's own panel: viewer.c
#ifndef VIEWER_H
#define VIEWER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app_api.h"
typedef struct { uint32_t frame; const char *time, *rank; double tally; int log_n; const char *(*log)(int i); } viewer_info_t;
bool viewer_init(const struct app_api *api);
bool viewer_due(void);                      // shown, and it's time to draw
void viewer_draw(const viewer_info_t *i);
int viewer_status(char *reply, int n);
void viewer_set(bool on);
#endif
