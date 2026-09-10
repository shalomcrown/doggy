//go:build !windows

package main

import (
	"os"
	"os/signal"
	"syscall"
	"time"
)

// ================================================================================

func maybeRunWindowsService(listen, webRoot, localFile, serialOverride string, poll time.Duration) bool {
	return false
}

// ================================================================================

func notifyProcessStop() <-chan struct{} {
	stop := make(chan struct{})
	go func() {
		sig := make(chan os.Signal, 1)
		signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
		<-sig
		close(stop)
	}()
	return stop
}
