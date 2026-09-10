package atcmd

import (
	"bytes"
	"io"
	"strings"
	"testing"
	"time"

	"doggy-lora/internal/settings"
)

// ================================================================================

func TestCommandsDisabled(t *testing.T) {
	if Commands(settings.Settings{Enabled: false, Txch: 18}) != nil {
		t.Fatal("disabled radio must not emit AT")
	}
}

// ================================================================================

func TestCommandsTxRxAndMode(t *testing.T) {
	lines := Commands(settings.Settings{
		Enabled: true,
		Band:    "HF",
		Txch:    18,
		Rxch:    18,
		LBT:     255,
	})
	joined := strings.Join(lines, "\n")
	if strings.Contains(joined, "AT+BAND") || strings.Contains(joined, "AT+REGION") {
		t.Fatalf("must not emit BAND/REGION: %v", lines)
	}
	if lines[0] != "+++" {
		t.Fatalf("first line must enter AT mode: %v", lines)
	}
	if strings.Contains(joined, "AT+TXCH=18") == false {
		t.Fatalf("missing TXCH: %v", lines)
	}
	if strings.Contains(joined, "AT+RXCH=18") == false {
		t.Fatalf("missing RXCH: %v", lines)
	}
	if strings.Contains(joined, "AT+LBT=255") == false {
		t.Fatalf("missing exact LBT value: %v", lines)
	}
	if strings.Contains(joined, "AT+EXIT") == false {
		t.Fatalf("missing EXIT: %v", lines)
	}
}

// ================================================================================

func TestCommandsFactoryLBTDefault(t *testing.T) {
	lines := Commands(settings.Settings{
		Enabled: true,
		Txch:    18,
		Rxch:    18,
	})
	if strings.Contains(strings.Join(lines, "\n"), "AT+LBT=0") == false {
		t.Fatalf("missing factory LBT value: %v", lines)
	}
}

// ================================================================================

func TestFrequencyMHz(t *testing.T) {
	if settings.FrequencyMHz("HF", 18) != 868 {
		t.Fatal("HF ch 18 is 868 MHz")
	}
	if settings.FrequencyMHz("LF", 23) != 433 {
		t.Fatal("LF ch 23 is 433 MHz")
	}
}

// ================================================================================

func TestEncodeCRLF(t *testing.T) {
	got := Encode([]string{"AT+TXCH=18"})
	if bytes.Equal(got, []byte("AT+TXCH=18\r\n")) == false {
		t.Fatalf("got %q", got)
	}
}

// ================================================================================

type fakePort struct {
	writes []string
	in     bytes.Buffer
}

// ================================================================================

func (f *fakePort) Write(p []byte) (int, error) {
	f.writes = append(f.writes, string(p))
	_, _ = f.in.WriteString("OK\r\n")
	return len(p), nil
}

// ================================================================================

func (f *fakePort) Read(p []byte) (int, error) {
	if f.in.Len() == 0 {
		return 0, io.EOF
	}
	return f.in.Read(p)
}

// ================================================================================

func TestApplyWritesWhenChanged(t *testing.T) {
	savedDelay := afterPlusDelay
	afterPlusDelay = 0
	defer func() { afterPlusDelay = savedDelay }()

	port := &fakePort{}
	s := settings.Settings{Enabled: true, Band: "LF", Txch: 23, Rxch: 23}
	n, err := Apply(port, settings.Settings{}, s)
	if err != nil {
		t.Fatal(err)
	}
	if n == 0 {
		t.Fatal("expected writes")
	}
	joined := strings.Join(port.writes, "")
	if strings.Contains(joined, "AT+TXCH=23") == false {
		t.Fatalf("writes %q", port.writes)
	}
	if len(port.writes) < 5 {
		t.Fatalf("expected one write per AT line, got %v", port.writes)
	}
	if port.writes[0] != "+++\r\n" {
		t.Fatalf("first write %q", port.writes[0])
	}
	if port.writes[1] != "AT+LBT=0\r\n" {
		t.Fatalf("second write %q", port.writes[1])
	}
	if port.writes[2] != "AT+TXCH=23\r\n" {
		t.Fatalf("third write %q", port.writes[2])
	}

	port = &fakePort{}
	n, err = Apply(port, s, s)
	if err != nil {
		t.Fatal(err)
	}
	if n != 0 {
		t.Fatal("matching settings must not re-AT")
	}
}

// ================================================================================

type scriptedPort struct {
	writes  []string
	replies []string
	i       int
	in      bytes.Buffer
}

// ================================================================================

func (f *scriptedPort) Write(p []byte) (int, error) {
	f.writes = append(f.writes, string(p))
	if f.i < len(f.replies) {
		_, _ = f.in.WriteString(f.replies[f.i])
		f.i++
	}
	return len(p), nil
}

// ================================================================================

func (f *scriptedPort) Read(p []byte) (int, error) {
	if f.in.Len() == 0 {
		return 0, io.EOF
	}
	return f.in.Read(p)
}

// ================================================================================

func TestApplyStopsOnErrorBeforeNextCommand(t *testing.T) {
	savedDelay := afterPlusDelay
	afterPlusDelay = 0
	defer func() { afterPlusDelay = savedDelay }()

	port := &scriptedPort{replies: []string{"OK\r\n", "ERROR\r\n"}}
	s := settings.Settings{Enabled: true, Txch: 18, Rxch: 18}
	_, err := Apply(port, settings.Settings{}, s)
	if err == nil {
		t.Fatal("expected ERROR from LBT")
	}
	if len(port.writes) != 2 {
		t.Fatalf("must not send TXCH after ERROR: %v", port.writes)
	}
	if strings.Contains(strings.Join(port.writes, ""), "AT+TXCH") {
		t.Fatalf("sent TXCH after ERROR: %v", port.writes)
	}
}

// ================================================================================

func TestApplyDelaysAfterPlus(t *testing.T) {
	savedDelay := afterPlusDelay
	afterPlusDelay = 40 * time.Millisecond
	defer func() { afterPlusDelay = savedDelay }()

	port := &fakePort{}
	s := settings.Settings{Enabled: true, Txch: 18, Rxch: 18}
	start := time.Now()
	if _, err := Apply(port, settings.Settings{}, s); err != nil {
		t.Fatal(err)
	}
	if time.Since(start) < 40*time.Millisecond {
		t.Fatal("expected delay after +++")
	}
}
