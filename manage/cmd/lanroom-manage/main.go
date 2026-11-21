package main

import (
	"log"
	"net/http"
	"os"
	"strings"

	"lanroom/manage/internal/api"
)

func main() {
	server, err := api.New(api.ConfigFromEnv())
	if err != nil {
		log.Fatal(err)
	}
	addr := strings.TrimSpace(os.Getenv("LISTEN_ADDR"))
	if addr == "" {
		addr = strings.TrimSpace(os.Getenv("PORT"))
	}
	if addr == "" {
		addr = "8080"
	}
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}
	log.Printf("lanroom manage listening %s", addr)
	log.Fatal(http.ListenAndServe(addr, server.Handler()))
}
