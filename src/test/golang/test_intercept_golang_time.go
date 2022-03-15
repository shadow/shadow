package main

import (
	"fmt"
	"time"
)

// Validate that time is being intercepted.
// Won't pass natively.
func main() {
	var t0 = time.Now()
	var t1 = time.Now()

	if t0 != t1 {
		panic(fmt.Sprint(t0, " != ", t1))
	}
}
