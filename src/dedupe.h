#pragma once

#include <glib.h>

typedef struct DedupeState DedupeState;

/* ttl_us = 0 means keep until Valent exits (and forever on disk if persist_path).
 * persist_path may be NULL for tests (memory only). */
DedupeState *vn_dedupe_new (gint64 ttl_us, const char *persist_path);
void vn_dedupe_free (DedupeState *s);
void vn_dedupe_set_ttl (DedupeState *s, gint64 ttl_us);

char *vn_content_fp (const char *app, const char *pkg, const char *summary,
                      const char *body);

/* TRUE if this exact toast was already shown (within TTL). Always records. */
gboolean vn_dedupe_should_drop (DedupeState *s, const char *app, const char *pkg,
                                 const char *summary, const char *body, gint64 now_us);

/* Record without treating as a live toast (e.g. seed from the jsonl log). */
void vn_dedupe_ingest (DedupeState *s, const char *app, const char *pkg,
                       const char *summary, const char *body, gint64 ts_us);

void vn_dedupe_flush (DedupeState *s);
