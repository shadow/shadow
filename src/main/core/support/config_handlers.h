#include "main/bindings/c/bindings-opaque.h"

typedef void (*ConfigHandlerFn)(const ConfigOptions*);

void addConfigHandler(ConfigHandlerFn fun_ptr);
void runConfigHandlers(const ConfigOptions* config);

// Used to generate unique symbol names. See
// https://stackoverflow.com/questions/1597007/creating-c-macro-with-and-line-token-concatenation-with-positioning-macr
#define OPTIONS_TOKENPASTE(x, y) x##y
#define OPTIONS_TOKENPASTE2(x, y) OPTIONS_TOKENPASTE(x, y)

#define ADD_CONFIG_HANDLER(config_fn, value)                                                       \
    static void OPTIONS_TOKENPASTE2(_set_value_, __LINE__)(const ConfigOptions* config) {          \
        value = config_fn(config);                                                                 \
    }                                                                                              \
    __attribute__((constructor)) static void OPTIONS_TOKENPASTE2(_add_entry_, __LINE__)() {        \
        addConfigHandler(OPTIONS_TOKENPASTE2(_set_value_, __LINE__));                              \
    }
