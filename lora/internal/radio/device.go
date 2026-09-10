package radio

import (
	"io"
	"log"
	"os"
	"sync"

	"doggy-lora/internal/atcmd"
	"doggy-lora/internal/serialport"
	"doggy-lora/internal/settings"
)

// ================================================================================

type OpenFunc func(device string, baud int) (*os.File, error)

// ================================================================================

type Device struct {
	mu       sync.Mutex
	open     OpenFunc
	file     *os.File
	applied  settings.Settings
	haveFile bool
}

// ================================================================================

func New(open OpenFunc) *Device {
	if open == nil {
		open = serialport.Open
	}
	return &Device{open: open}
}

// ================================================================================

func (d *Device) Apply(desired settings.Settings) {
	desired = desired.Normalize()
	d.mu.Lock()
	defer d.mu.Unlock()
	if settings.Equal(d.applied, desired) && d.haveFile {
		return
	}
	if desired.Enabled && desired.Device == "" {
		log.Printf("lora AT skipped: no serial device")
		d.closeLocked()
		d.applied = desired
		return
	}
	if desired.Enabled == false {
		log.Printf("lora disabled")
		d.closeLocked()
		d.applied = desired
		return
	}
	if desired.BaudOK() == false || desired.DeviceOK() == false {
		log.Printf("lora AT skipped: bad device or baud")
		return
	}
	needOpen := d.haveFile == false || d.applied.Device != desired.Device || d.applied.Baud != desired.Baud
	if needOpen {
		d.closeLocked()
		f, err := d.open(desired.Device, desired.Baud)
		if err != nil {
			log.Printf("lora serial open %s baud %d: %v", desired.Device, desired.Baud, err)
			return
		}
		d.file = f
		d.haveFile = true
	}
	if _, err := atcmd.Apply(d.file, d.applied, desired); err != nil {
		log.Printf("lora AT write: %v", err)
		return
	}
	log.Printf("lora serial %s %d 8N1", desired.Device, desired.Baud)
	d.applied = desired
}

// ================================================================================

func (d *Device) Applied() settings.Settings {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.applied
}

// ================================================================================

func (d *Device) ReadWriter() io.ReadWriter {
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.haveFile == false {
		return nil
	}
	return d.file
}

// ================================================================================

func (d *Device) Close() {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.closeLocked()
}

// ================================================================================

func (d *Device) closeLocked() {
	if d.haveFile && d.file != nil {
		_ = d.file.Close()
	}
	d.file = nil
	d.haveFile = false
}
