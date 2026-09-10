package settings

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"

	"doggy-lora/internal/serialport"
)

const airKeyTrimCutset = " \t\f\v"

// Settings is the shared lora object from doggy.json or the operator file.
// Band is HF (850–930 MHz) or LF (410–490 MHz). Channels 0–80 map 1 MHz
// apart: HF MHz = 850+ch, LF MHz = 410+ch (Waveshare USB-TO-LoRa-xF).

// ================================================================================

type Settings struct {
	Enabled bool   `json:"enabled"`
	Device  string `json:"device"`
	Baud    int    `json:"baud"`
	Band    string `json:"band"`
	Txch    int    `json:"txch"`
	Rxch    int    `json:"rxch"`
	LBT     uint8  `json:"lbt"`
	AirKey  string `json:"air_key,omitempty"`
}

// ================================================================================

func (s *Settings) UnmarshalJSON(data []byte) error {
	type plain Settings
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(data, &fields); err != nil {
		return err
	}
	if _, legacy := fields["listen_before_talk"]; legacy {
		return fmt.Errorf("listen_before_talk was replaced by numeric lbt")
	}
	value := plain(Default())
	if err := json.Unmarshal(data, &value); err != nil {
		return err
	}
	*s = Settings(value)
	return nil
}

// ================================================================================

type fileEnvelope struct {
	Lora Settings `json:"lora"`
}

// ================================================================================

func ParseLoraJSON(text []byte) (Settings, error) {
	var object map[string]json.RawMessage
	if err := json.Unmarshal(text, &object); err != nil {
		return Settings{}, fmt.Errorf("lora json: %w", err)
	}
	if raw, ok := object["lora"]; ok {
		var nested Settings
		if err := json.Unmarshal(raw, &nested); err != nil {
			return Settings{}, fmt.Errorf("lora json: %w", err)
		}
		return nested, nil
	}
	var direct Settings
	if err := json.Unmarshal(text, &direct); err != nil {
		return Settings{}, fmt.Errorf("lora json: %w", err)
	}
	return direct, nil
}

// ================================================================================

func LoadFile(path string) (Settings, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return Settings{}, err
	}
	return ParseLoraJSON(raw)
}

// ================================================================================

func SaveFile(path string, s Settings) error {
	raw, err := json.MarshalIndent(fileEnvelope{Lora: s}, "", "  ")
	if err != nil {
		return err
	}
	raw = append(raw, '\n')
	mode := os.FileMode(0644)
	if strings.Trim(s.AirKey, airKeyTrimCutset) != "" {
		mode = 0600
		if err := os.Chmod(path, mode); err != nil && os.IsNotExist(err) == false {
			return err
		}
	}
	if err := os.WriteFile(path, raw, mode); err != nil {
		return err
	}
	return os.Chmod(path, mode)
}

// ================================================================================

func (s Settings) Public() map[string]any {
	s = s.Normalize()
	return map[string]any{
		"enabled":     s.Enabled,
		"device":      s.Device,
		"baud":        s.Baud,
		"band":        s.Band,
		"txch":        s.Txch,
		"rxch":        s.Rxch,
		"lbt":         s.LBT,
		"air_key_set": strings.Trim(s.AirKey, airKeyTrimCutset) != "",
	}
}

// ================================================================================

func Equal(a, b Settings) bool {
	a, b = a.Normalize(), b.Normalize()
	return a.Enabled == b.Enabled && a.Device == b.Device && a.Baud == b.Baud &&
		a.Band == b.Band && a.Txch == b.Txch && a.Rxch == b.Rxch &&
		a.LBT == b.LBT
}

// ================================================================================

func Default() Settings {
	return Settings{
		Band: "HF",
		Txch: 18,
		Rxch: 18,
		Baud: 115200,
		LBT:  0,
	}
}

// ================================================================================

func (s Settings) Normalize() Settings {
	s.Device = strings.TrimSpace(s.Device)
	s.Band = strings.ToUpper(strings.TrimSpace(s.Band))
	if s.Band != "LF" {
		s.Band = "HF"
	}
	if s.Baud == 0 {
		s.Baud = 115200
	}
	s.AirKey = strings.Trim(s.AirKey, airKeyTrimCutset)
	return s
}

// ================================================================================

func (s Settings) ChannelsOK() bool {
	return s.Txch >= 0 && s.Txch <= 80 && s.Rxch >= 0 && s.Rxch <= 80
}

// ================================================================================

func (s Settings) BaudOK() bool {
	return serialport.BaudOK(s.Baud)
}

// ================================================================================

func (s Settings) DeviceOK() bool {
	return strings.ContainsAny(s.Device, "\r\n") == false
}

// ================================================================================

func FrequencyMHz(band string, ch int) int {
	if ch < 0 || ch > 80 {
		return 0
	}
	if strings.ToUpper(strings.TrimSpace(band)) == "LF" {
		return 410 + ch
	}
	return 850 + ch
}
