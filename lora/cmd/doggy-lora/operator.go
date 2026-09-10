package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"doggy-lora/internal/hop"
	"doggy-lora/internal/operator"
	"doggy-lora/internal/radio"
	"doggy-lora/internal/serialport"
	"doggy-lora/internal/settings"
)

// ================================================================================

func runOperator(listen, webRoot, localFile, serialOverride string, poll time.Duration) {
	if maybeRunWindowsService(listen, webRoot, localFile, serialOverride, poll) {
		return
	}
	stop := notifyProcessStop()
	if err := serveOperator(listen, webRoot, localFile, serialOverride, poll, stop); err != nil {
		log.Fatal(err)
	}
}

// ================================================================================

func serveOperator(listen, webRoot, localFile, serialOverride string, poll time.Duration, stop <-chan struct{}) error {
	if err := os.MkdirAll(filepath.Dir(localFile), 0755); err != nil {
		log.Printf("lora config dir: %v", err)
	}
	dev := radio.New(serialport.Open)
	rt := hop.New(hop.RoleOperator, dev, "")
	srv := &http.Server{Addr: listen, Handler: operator.New(webRoot, localFile, rt)}
	go monitorLocalFile(rt, localFile, serialOverride, poll, stop)
	log.Printf("operator listening on http://%s", listen)
	errCh := make(chan error, 1)
	go func() {
		errCh <- srv.ListenAndServe()
	}()
	select {
	case <-stop:
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = srv.Shutdown(ctx)
		rt.Sync(settings.Settings{Enabled: false})
		return nil
	case err := <-errCh:
		if err == http.ErrServerClosed {
			return nil
		}
		return err
	}
}

// ================================================================================

func monitorLocalFile(rt *hop.Runtime, localFile, serialOverride string, poll time.Duration, stop <-chan struct{}) {
	tick := time.NewTicker(poll)
	defer tick.Stop()
	for {
		select {
		case <-stop:
			return
		case <-tick.C:
			desired, err := settings.LoadFile(localFile)
			if err != nil {
				continue
			}
			if serialOverride != "" {
				desired.Device = serialOverride
			}
			rt.Sync(desired)
		}
	}
}
