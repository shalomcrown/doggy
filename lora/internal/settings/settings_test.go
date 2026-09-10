package settings

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// ================================================================================

func TestDefaultBaudAndDevice(t *testing.T) {
	d := Default()
	if d.Baud != 115200 {
		t.Fatal(d.Baud)
	}
	if d.Normalize().BaudOK() == false {
		t.Fatal("default baud")
	}
	zero := Settings{}.Normalize()
	if zero.Baud != 115200 {
		t.Fatalf("zero baud became %d", zero.Baud)
	}
}

// ================================================================================

func TestDeviceRejectsNewline(t *testing.T) {
	s := Settings{Device: "/dev/ttyUSB0\n"}
	if s.DeviceOK() {
		t.Fatal("newline must fail")
	}
}

// ================================================================================

func TestPublicOmitsAirKey(t *testing.T) {
	s := Settings{AirKey: strings.Repeat("ab", 32), Band: "HF", Txch: 18, Rxch: 18, Baud: 115200}
	p := s.Public()
	if p["air_key_set"] != true {
		t.Fatal(p)
	}
	if _, ok := p["air_key"]; ok {
		t.Fatal("air_key leaked")
	}
}

// ================================================================================

func TestSaveFileTightensModeWhenPassphraseIsAdded(t *testing.T) {
	path := filepath.Join(t.TempDir(), "lora.json")
	if err := SaveFile(path, Default()); err != nil {
		t.Fatal(err)
	}
	if err := SaveFile(path, Settings{AirKey: "correct horse battery staple"}); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if got := info.Mode().Perm(); got != 0600 {
		t.Fatalf("settings mode %04o, want 0600", got)
	}
}
