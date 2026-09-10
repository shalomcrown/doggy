package serialport

import "testing"

// ================================================================================

func TestBaudOK(t *testing.T) {
	if BaudOK(115200) == false {
		t.Fatal("115200 must be allowed")
	}
	if BaudOK(1200) {
		t.Fatal("1200 must be rejected")
	}
}
