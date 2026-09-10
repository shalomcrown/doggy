//go:build windows

package main

import (
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"golang.org/x/sys/windows/svc"
)

const windowsServiceName = "DoggyLoraOperator"

// ================================================================================

type operatorService struct {
	listen         string
	webRoot        string
	localFile      string
	serialOverride string
	poll           time.Duration
}

// ================================================================================

func (m *operatorService) Execute(args []string, r <-chan svc.ChangeRequest, changes chan<- svc.Status) (bool, uint32) {
	changes <- svc.Status{State: svc.StartPending}
	stop := make(chan struct{})
	errCh := make(chan error, 1)
	go func() {
		errCh <- serveOperator(m.listen, m.webRoot, m.localFile, m.serialOverride, m.poll, stop)
	}()
	changes <- svc.Status{State: svc.Running, Accepts: svc.AcceptStop | svc.AcceptShutdown}
	for c := range r {
		switch c.Cmd {
		case svc.Interrogate:
			changes <- c.CurrentStatus
		case svc.Stop, svc.Shutdown:
			close(stop)
			changes <- svc.Status{State: svc.StopPending}
			select {
			case err := <-errCh:
				if err != nil {
					log.Printf("operator stop: %v", err)
				}
			case <-time.After(6 * time.Second):
			}
			return false, 0
		default:
			changes <- c.CurrentStatus
		}
	}
	return false, 0
}

// ================================================================================

func maybeRunWindowsService(listen, webRoot, localFile, serialOverride string, poll time.Duration) bool {
	isSvc, err := svc.IsWindowsService()
	if err != nil {
		log.Printf("windows service detect: %v", err)
		return false
	}
	if isSvc == false {
		return false
	}
	err = svc.Run(windowsServiceName, &operatorService{
		listen:         listen,
		webRoot:        webRoot,
		localFile:      localFile,
		serialOverride: serialOverride,
		poll:           poll,
	})
	if err != nil {
		log.Fatal(err)
	}
	return true
}

// ================================================================================

func notifyProcessStop() <-chan struct{} {
	stop := make(chan struct{})
	go func() {
		sig := make(chan os.Signal, 1)
		signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
		<-sig
		close(stop)
	}()
	return stop
}
