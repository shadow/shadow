#include <glib.h>
#include <unistd.h>

#include "test/test_glib_helpers.h"

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_run();

    return 0;
}
