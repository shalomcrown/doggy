package atcmd

import (
	"fmt"

	"doggy-lora/internal/settings"
)

// Commands is the Waveshare USB-TO-LoRa-xF AT sequence:
// +++ enters command mode, TXCH/RXCH are 0–80, AT+EXIT returns to stream mode.

// ================================================================================

func Commands(s settings.Settings) []string {
	s = s.Normalize()
	if s.Enabled == false || s.ChannelsOK() == false {
		return nil
	}

	return []string{
		"+++",
		fmt.Sprintf("AT+TXCH=%d", s.Txch),
		fmt.Sprintf("AT+RXCH=%d", s.Rxch),
		"AT+EXIT",
	}
}

// ================================================================================

func Encode(lines []string) []byte {
	var buf []byte
	for _, line := range lines {
		buf = append(buf, line...)
		buf = append(buf, '\r', '\n')
	}
	return buf
}
