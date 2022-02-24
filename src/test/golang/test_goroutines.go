package main

import (
    "fmt"
    "runtime"
    "sync"
    "time"
)

func println(args ...interface{}) {
    // Runtime error on go 1.17
    // https://github.com/golang/go/issues/47873
    if (runtime.Version() != "go1.17") {
        fmt.Println(args...);
    }
}

func worker(wg *sync.WaitGroup, from int) {
    defer wg.Done()
    for i := 0; i < 10; i++ {
        println("Worker:", from, " iteration:", i)
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
    println("done")
}
