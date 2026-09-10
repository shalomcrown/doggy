package serialport

import (
	"fmt"
	"os"
)

// ================================================================================

func BaudOK(baud int) bool {
	switch baud {
	case 9600, 19200, 38400, 57600, 115200:
		return true
	default:
		return false
	}
}

// ================================================================================

func Open(device string, baud int) (*os.File, error) {
	if BaudOK(baud) == false {
		return nil, fmt.Errorf("unsupported baud %d", baud)
	}
	f, err := os.OpenFile(device, os.O_RDWR, 0)
	if err != nil {
		return nil, err
	}
	if err := configure(f, baud); err != nil {
		_ = f.Close()
		return nil, err
	}
	return f, nil
}
