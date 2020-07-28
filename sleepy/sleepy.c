#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {

    for (size_t idx = 0; idx < 10; ++idx) {
      // sleep(1);
    }

    write(0, "hello\n", 7);

    return 0;
}
