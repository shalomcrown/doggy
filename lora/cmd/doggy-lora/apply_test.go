package main

import (
	"os"
	"testing"

	"doggy-lora/internal/radio"
	"doggy-lora/internal/settings"
)

// ================================================================================

func TestApplyToPortOpensDeviceAndBaud(t *testing.T) {
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	defer r.Close()
	var gotDev string
	var gotBaud int
	dev := radio.New(func(device string, baud int) (*os.File, error) {
		gotDev = device
		gotBaud = baud
		return w, nil
	})
	dev.Apply(settings.Settings{
		Enabled: true,
		Device:  "/dev/ttyUSB0",
		Baud:    115200,
		Txch:    18,
		Rxch:    18,
	})
	_ = w.Close()
	if gotDev != "/dev/ttyUSB0" || gotBaud != 115200 {
		t.Fatalf("open %s %d", gotDev, gotBaud)
	}
}
