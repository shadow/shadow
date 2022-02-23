package main

import (
	"net/http"
)

type SimpleHandler struct{}

func (h *SimpleHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	data := []byte("Hi, I'm a simple web server\n")
	w.Write(data)
}

func main() {
	handler := &SimpleHandler{}
	http.ListenAndServe(":80", handler)
}
