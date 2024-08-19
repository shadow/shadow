package main

import (
    "time"
)

func main() {
    // Just sleep. We're waiting for a SIGTERM to be delivered externally.
    // This is a regression test for handling golang's default SIGTERM handler,
    // which appears to reuse an in-use stack just before exiting.
    // See https://github.com/shadow/shadow/issues/3395.
    time.Sleep(10000 * time.Second)
}
