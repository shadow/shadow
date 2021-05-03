#include <gmodule.h>
#include <stdbool.h>

#include "main/core/support/config_handlers.h"

static GArray* experimentalOptions = NULL;

void addConfigHandler(ConfigHandlerFn fun_ptr) {
    if (experimentalOptions == NULL) {
        experimentalOptions = g_array_new(TRUE, TRUE, sizeof(ConfigHandlerFn));
    }
    g_array_append_val(experimentalOptions, fun_ptr);
}

void runConfigHandlers(const ConfigOptions* config) {
    if (experimentalOptions == NULL) {
        return;
    }
    for (int i = 0; i < experimentalOptions->len; i++) {
        ConfigHandlerFn fn = g_array_index(experimentalOptions, ConfigHandlerFn, i);
        fn(config);
    }
    g_array_free(experimentalOptions, true);
    experimentalOptions = NULL;
}
