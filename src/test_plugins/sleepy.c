#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {

    printf("sleeping for 1 second...\n");
    sleep(1);
    printf("...complete, I slept for 1 second.\n");

    return 0;
}
