package main

import (
	"fmt"
	"net"
	"runtime"
)

func println(args ...interface{}) {
	// Runtime error on go 1.17
	// https://github.com/golang/go/issues/47873
	if (runtime.Version() != "go1.17") {
		fmt.Println(args...);
	}
}

func main() {
	ifaces, err := net.Interfaces()
	if err != nil {
		panic(err)
	}
	for _, iface := range ifaces {
		addrs, err := iface.Addrs()
		if err != nil {
			panic(err)
		}
		for _, addr := range addrs {
			println(addr.Network(), addr.String())
		}
	}
}
