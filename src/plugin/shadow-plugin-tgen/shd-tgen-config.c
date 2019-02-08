#include "shd-tgen.h"

gint tgenconfig_gethostname(gchar* name, size_t len) {
    gchar* tgenip = getenv("TGENHOSTNAME");
    if (tgenip != NULL) {
        return -(g_snprintf(name, len, "%s", tgenip) < 0);
    } else {
        return gethostname(name, len);
    }
}

gchar* tgenconfig_getIP() {
    return getenv("TGENIP");
}
