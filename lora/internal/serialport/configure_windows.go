//go:build windows

package serialport

import (
	"fmt"
	"os"

	"golang.org/x/sys/windows"
)

// ================================================================================

func configure(f *os.File, baud int) error {
	h := windows.Handle(f.Fd())
	var dcb windows.DCB
	if err := windows.GetCommState(h, &dcb); err != nil {
		return fmt.Errorf("serial getcommstate: %w", err)
	}
	dcb.BaudRate = uint32(baud)
	dcb.ByteSize = 8
	dcb.Parity = windows.NOPARITY
	dcb.StopBits = windows.ONESTOPBIT
	if err := windows.SetCommState(h, &dcb); err != nil {
		return fmt.Errorf("serial setcommstate: %w", err)
	}
	return nil
}
