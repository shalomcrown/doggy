package atcmd

import (
	"fmt"

	"doggy-lora/internal/settings"
)

// Commands is the Waveshare USB-TO-LoRa-xF AT sequence:
// +++ enters command mode, LBT is 0–255, TXCH/RXCH are 0–80, and AT+EXIT
// returns to stream mode.

// ================================================================================

func Commands(s settings.Settings) []string {
	s = s.Normalize()
	if s.Enabled == false || s.ChannelsOK() == false {
		return nil
	}

	return []string{
		"+++",
		fmt.Sprintf("AT+LBT=%d", s.LBT),
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
