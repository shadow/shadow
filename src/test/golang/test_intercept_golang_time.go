package main

import (
	"fmt"
	"time"
)

// Validate that time is being intercepted.
// Won't pass natively. (At least without resetting the system clock)
func main() {
	var shadowSimStartTime = time.Date(2000, time.January, 1, 1, 0, 0, 0, time.UTC)
	var now = time.Now()
	var dt = now.Sub(shadowSimStartTime)
	var maxDelta, _ = time.ParseDuration("5m")
	if dt > maxDelta {
		panic(fmt.Sprint("Time is now ", now, "; ", dt, " since sim start of ", shadowSimStartTime))
	}
}
