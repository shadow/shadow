#define _POSIX_C_SOURCE 200809L

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <string>
#include <vector>

#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>

static const char *kSleepExe = "/bin/sleep";

union ShortString {
    char contents[64];
};

using Args = std::vector<ShortString>;
extern char **environ;

static ShortString to_short_string_(const char *s) {
    ShortString ret = {0};
    std::strncpy(ret.contents, s, 64);
    return ret;
}

static Args make_args_() {
    Args args;
    args.push_back(to_short_string_(kSleepExe));
    args.push_back(to_short_string_("10"));
    args.push_back(to_short_string_("\0"));
    return args;
}

static pid_t fork_sleep_(const Args &args) {
    pid_t child_pid = {0};

    char *argv[] = {"sleep", "100", 0};

    auto rc = posix_spawn(&child_pid, kSleepExe, nullptr, nullptr,
                          argv, environ);

    if (rc == -1) {
        std::fprintf(stderr, "Error %d on fork.\n", rc);
        std::abort();
    }

    return child_pid;
}

int main(int argc, char **argv) {

    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s n", argv[0]);
        return -1;
    }

    auto n = std::stoi(argv[1]);

    auto args = make_args_();
    std::vector<pid_t> pids;

    for (int idx = 0; idx < n; ++idx) {
        pids.push_back(fork_sleep_(args));
    }

    std::printf("Waiting for cleanup.\n");

    int status = 0;
    for (auto pid : pids) {
        waitpid(pid, &status, 0);
        std::printf("%d exited normally: %d\n", pid, WIFEXITED(status));
    }

    return 0;
}
