package main

import (
    "fmt"
    "sync"
    "time"
)

func worker(wg *sync.WaitGroup, from int) {
    defer wg.Done()
    for i := 0; i < 10; i++ {
        fmt.Println("Worker:", from, " iteration:", i)
        // Use a relatively short sleep so that this test passes quickly when run natively.
        // *Too* short (e.g. 1 Millisecond) causes
        // https://github.com/shadow/shadow/issues/1932 not to reproduce in
        // ptrace-mode.
        time.Sleep(10 * time.Millisecond)
    }
}

func main() {
    var wg sync.WaitGroup
    for i := 0; i < 10; i++ {
      wg.Add(1)
      go worker(&wg, i)
    }
    wg.Wait()
    fmt.Println("done")
}
