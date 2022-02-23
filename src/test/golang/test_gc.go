package main

import (
    "fmt"
    "runtime"
)

func main() {
    for i := 0; i < 100; i++ {
      // Generate some garbage
      garbage := make([]int, 10000)
      // Runtime error on go 1.17
      // https://github.com/golang/go/issues/47873
      if (runtime.Version() != "go1.17") {
        fmt.Println("loop", i, ": ", &garbage[0])
      }
    }
}
