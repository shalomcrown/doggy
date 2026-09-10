//go:build linux

package serialport

import (
	"fmt"
	"os"

	"golang.org/x/sys/unix"
)

// ================================================================================

func configure(f *os.File, baud int) error {
	speed, err := linuxSpeed(baud)
	if err != nil {
		return err
	}
	fd := int(f.Fd())
	t, err := unix.IoctlGetTermios(fd, unix.TCGETS)
	if err != nil {
		return fmt.Errorf("serial tcgets: %w", err)
	}
	t.Iflag &^= unix.IGNBRK | unix.BRKINT | unix.PARMRK | unix.ISTRIP | unix.INLCR | unix.IGNCR | unix.ICRNL | unix.IXON
	t.Oflag &^= unix.OPOST
	t.Lflag &^= unix.ECHO | unix.ECHONL | unix.ICANON | unix.ISIG | unix.IEXTEN
	t.Cflag = unix.CS8 | unix.CREAD | unix.CLOCAL | speed
	t.Ispeed = speed
	t.Ospeed = speed
	t.Cc[unix.VMIN] = 0
	t.Cc[unix.VTIME] = 1
	if err := unix.IoctlSetTermios(fd, unix.TCSETS, t); err != nil {
		return fmt.Errorf("serial tcsets: %w", err)
	}
	return nil
}

// ================================================================================

func linuxSpeed(baud int) (uint32, error) {
	switch baud {
	case 9600:
		return unix.B9600, nil
	case 19200:
		return unix.B19200, nil
	case 38400:
		return unix.B38400, nil
	case 57600:
		return unix.B57600, nil
	case 115200:
		return unix.B115200, nil
	default:
		return 0, fmt.Errorf("unsupported baud %d", baud)
	}
}
