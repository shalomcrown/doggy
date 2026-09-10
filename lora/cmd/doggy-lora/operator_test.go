package main

import (
	"path/filepath"
	"testing"
	"time"
)

// ================================================================================

func TestMonitorLocalFileStops(t *testing.T) {
	stop := make(chan struct{})
	done := make(chan struct{})
	go func() {
		monitorLocalFile(nil, filepath.Join(t.TempDir(), "missing.json"), "", time.Hour, stop)
		close(done)
	}()
	close(stop)
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("monitorLocalFile did not stop")
	}
}
