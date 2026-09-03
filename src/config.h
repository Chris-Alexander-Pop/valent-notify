#pragma once

#include "rewrite.h"

void notify_config_free (NotifyConfig *cfg);

/* Load ~/.config/valent-notify/config.json (or $VALENT_NOTIFY_CONFIG). */
gboolean notify_config_load (NotifyConfig *cfg, GError **error);

/* Reload when mtime changes. Returns TRUE if a new file was applied. */
gboolean notify_config_reload_if_changed (NotifyConfig *cfg);

const char *notify_config_path (void);
const char *notify_state_dir (void);
