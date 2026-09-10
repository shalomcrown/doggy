//go:build !linux && !windows

package serialport

import "os"

// ================================================================================

func configure(f *os.File, baud int) error {
	return nil
}
