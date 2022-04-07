package main

import (
	"sync"
	"time"
)

func busy(wg *sync.WaitGroup) {
	t0 := time.Now()
	t_finish := t0.Add(100 * time.Millisecond)
	for time.Now().Before(t_finish) {
		// Spin.

		// For this test to pass, time must eventually move forward in such a
		// spin loop.

		// Really, though, this test is intended to test compatibility with
		// golang's preemption mechanism, which will eventually interrupt with
		// a signal to allow other goroutines to run.
	}
	wg.Done()
}

func main() {
	var wg sync.WaitGroup
	for i := 0; i < 2; i++ {
		wg.Add(1)
		go busy(&wg)
	}
	wg.Wait()
}
