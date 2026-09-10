package atcmd

import (
	"fmt"
	"io"
	"log"
	"strings"
	"time"

	"doggy-lora/internal/settings"
)

// afterPlusDelay is how long to wait after +++ before reading a reply or
// sending the next AT command. The dongle needs this to enter command mode.
var afterPlusDelay = 200 * time.Millisecond

var replyTimeout = time.Second

// ================================================================================

func Apply(port io.ReadWriter, applied, desired settings.Settings) (int, error) {
	desired = desired.Normalize()
	applied = applied.Normalize()
	if settings.Equal(applied, desired) {
		return 0, nil
	}

	lines := Commands(desired)
	if len(lines) == 0 {
		log.Printf("lora AT skip (disabled or empty)")
		return 0, nil
	}

	written := 0
	for _, line := range lines {
		n, err := port.Write(Encode([]string{line}))
		written += n
		if err != nil {
			return written, err
		}
		log.Printf("lora AT %s", line)
		if line == "+++" && afterPlusDelay > 0 {
			time.Sleep(afterPlusDelay)
		}
		reply, err := readReply(port, replyTimeout)
		if err != nil {
			if line == "+++" {
				log.Printf("lora AT +++ no reply (%v); continuing", err)
				continue
			}
			return written, err
		}
		log.Printf("lora AT reply %s", compactReply(reply))
		if replyFailed(reply) {
			return written, fmt.Errorf("AT %s: %s", line, compactReply(reply))
		}
		if line != "+++" && replyOK(reply) == false {
			return written, fmt.Errorf("AT %s: no OK in %q", line, compactReply(reply))
		}
	}
	return written, nil
}

// ================================================================================

func readReply(port io.Reader, timeout time.Duration) (string, error) {
	if d, ok := port.(interface{ SetReadDeadline(time.Time) error }); ok {
		_ = d.SetReadDeadline(time.Now().Add(timeout))
		defer func() { _ = d.SetReadDeadline(time.Time{}) }()
	}

	buf := make([]byte, 0, 256)
	tmp := make([]byte, 128)
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) || timeout == 0 {
		n, err := port.Read(tmp)
		if n > 0 {
			buf = append(buf, tmp[:n]...)
			s := string(buf)
			if replyOK(s) || replyFailed(s) {
				return s, nil
			}
		}
		if err != nil {
			if len(buf) > 0 {
				return string(buf), nil
			}
			return "", err
		}
		if timeout == 0 {
			break
		}
	}
	if len(buf) > 0 {
		return string(buf), nil
	}
	return "", fmt.Errorf("AT reply timeout")
}

// ================================================================================

func replyOK(s string) bool {
	u := strings.ToUpper(s)
	return strings.Contains(u, "OK") && strings.Contains(u, "ERROR") == false
}

// ================================================================================

func replyFailed(s string) bool {
	return strings.Contains(strings.ToUpper(s), "ERROR")
}

// ================================================================================

func compactReply(s string) string {
	s = strings.TrimSpace(s)
	s = strings.ReplaceAll(s, "\r", "")
	s = strings.ReplaceAll(s, "\n", " ")
	if len(s) > 80 {
		return s[:80]
	}
	return s
}
