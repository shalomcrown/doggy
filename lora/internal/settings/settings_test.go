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
	if d.LBT != 0 {
		t.Fatalf("LBT default %d, want factory value 0", d.LBT)
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

func TestLBTDefaultsAndRange(t *testing.T) {
	s, err := ParseLoraJSON([]byte(`{"lora":{"band":"HF","txch":18,"rxch":18}}`))
	if err != nil {
		t.Fatal(err)
	}
	if s.LBT != 0 {
		t.Fatalf("missing lbt became %d, want 0", s.LBT)
	}
	max, err := ParseLoraJSON([]byte(`{"lora":{"lbt":255}}`))
	if err != nil {
		t.Fatal(err)
	}
	if max.LBT != 255 {
		t.Fatalf("maximum lbt became %d", max.LBT)
	}
	if _, err := ParseLoraJSON([]byte(`{"lora":{"lbt":256}}`)); err == nil {
		t.Fatal("lbt above 255 must fail")
	}
	if Equal(s, Settings{Band: "HF", Txch: 18, Rxch: 18, LBT: 1}) {
		t.Fatal("LBT-only change must not compare equal")
	}
	if _, err := ParseLoraJSON([]byte(`{"lora":{"listen_before_talk":true}}`)); err == nil {
		t.Fatal("legacy boolean LBT config must fail with a migration error")
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
