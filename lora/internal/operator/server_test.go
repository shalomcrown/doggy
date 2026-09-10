package operator

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// ================================================================================

func TestSetupPageAndLoraAPI(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "lora.html"), []byte("<h1>LoRa setup</h1>"), 0644); err != nil {
		t.Fatal(err)
	}
	cfg := filepath.Join(dir, "lora.json")
	h := New(dir, cfg, nil)
	ts := httptest.NewServer(h)
	defer ts.Close()

	res, err := http.Get(ts.URL + "/")
	if err != nil {
		t.Fatal(err)
	}
	body, _ := io.ReadAll(res.Body)
	res.Body.Close()
	if strings.Contains(string(body), "LoRa setup") == false {
		t.Fatalf("setup page: %s", body)
	}

	res, err = http.Get(ts.URL + "/api/lora")
	if err != nil {
		t.Fatal(err)
	}
	raw0, _ := io.ReadAll(res.Body)
	res.Body.Close()
	var empty struct {
		Lora struct {
			Band string `json:"band"`
			Txch int    `json:"txch"`
			Rxch int    `json:"rxch"`
			Baud int    `json:"baud"`
		} `json:"lora"`
	}
	if err := json.Unmarshal(raw0, &empty); err != nil {
		t.Fatal(err)
	}
	if empty.Lora.Band != "HF" || empty.Lora.Txch != 18 || empty.Lora.Rxch != 18 || empty.Lora.Baud != 115200 {
		t.Fatalf("missing file defaults %+v", empty)
	}

	put, err := http.NewRequest(http.MethodPut, ts.URL+"/api/lora", strings.NewReader(
		`{"lora":{"enabled":true,"device":"/dev/ttyUSB0","baud":115200,"band":"LF","txch":23,"rxch":23,"air_key":"correct horse battery staple"}}`))
	if err != nil {
		t.Fatal(err)
	}
	put.Header.Set("Content-Type", "application/json")
	res, err = http.DefaultClient.Do(put)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != 200 {
		t.Fatalf("put status %d", res.StatusCode)
	}
	res.Body.Close()

	res, err = http.Get(ts.URL + "/api/lora")
	if err != nil {
		t.Fatal(err)
	}
	raw, _ := io.ReadAll(res.Body)
	res.Body.Close()
	var got struct {
		Lora struct {
			Band   string `json:"band"`
			Txch   int    `json:"txch"`
			Rxch   int    `json:"rxch"`
			Baud   int    `json:"baud"`
			Device string `json:"device"`
		} `json:"lora"`
	}
	if err := json.Unmarshal(raw, &got); err != nil {
		t.Fatal(err)
	}
	if got.Lora.Band != "LF" || got.Lora.Txch != 23 || got.Lora.Rxch != 23 ||
		got.Lora.Baud != 115200 || got.Lora.Device != "/dev/ttyUSB0" {
		t.Fatalf("saved %+v", got)
	}
	var public map[string]map[string]json.RawMessage
	if err := json.Unmarshal(raw, &public); err != nil {
		t.Fatal(err)
	}
	if _, exists := public["lora"]["air_key"]; exists {
		t.Fatalf("GET returned air_key: %s", raw)
	}
	if strings.Contains(string(raw), `"air_key_set":true`) == false {
		t.Fatalf("air_key_set: %s", raw)
	}

	keep, err := http.NewRequest(http.MethodPut, ts.URL+"/api/lora", strings.NewReader(
		`{"lora":{"enabled":true,"device":"/dev/ttyUSB0","baud":115200,"band":"LF","txch":24,"rxch":23}}`))
	if err != nil {
		t.Fatal(err)
	}
	keep.Header.Set("Content-Type", "application/json")
	res, err = http.DefaultClient.Do(keep)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != http.StatusOK {
		t.Fatalf("omitted passphrase status %d", res.StatusCode)
	}
	res.Body.Close()
	savedRaw, err := os.ReadFile(cfg)
	if err != nil {
		t.Fatal(err)
	}
	var saved struct {
		Lora struct {
			AirKey string `json:"air_key"`
		} `json:"lora"`
	}
	if err := json.Unmarshal(savedRaw, &saved); err != nil {
		t.Fatal(err)
	}
	if saved.Lora.AirKey != "correct horse battery staple" {
		t.Fatal("omitted passphrase did not keep the stored value")
	}
	keep, err = http.NewRequest(http.MethodPut, ts.URL+"/api/lora", strings.NewReader(
		`{"lora":{"enabled":true,"device":"/dev/ttyUSB0","baud":115200,"band":"LF","txch":24,"rxch":23,"air_key":""}}`))
	if err != nil {
		t.Fatal(err)
	}
	keep.Header.Set("Content-Type", "application/json")
	res, err = http.DefaultClient.Do(keep)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != http.StatusOK {
		t.Fatalf("empty passphrase status %d", res.StatusCode)
	}
	res.Body.Close()
	savedRaw, err = os.ReadFile(cfg)
	if err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(savedRaw, &saved); err != nil {
		t.Fatal(err)
	}
	if saved.Lora.AirKey != "correct horse battery staple" {
		t.Fatal("empty passphrase did not keep the stored value")
	}

	bad, err := http.NewRequest(http.MethodPut, ts.URL+"/api/lora", strings.NewReader(
		`{"lora":{"txch":81,"rxch":18}}`))
	if err != nil {
		t.Fatal(err)
	}
	bad.Header.Set("Content-Type", "application/json")
	res, err = http.DefaultClient.Do(bad)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != http.StatusBadRequest {
		t.Fatalf("out of range txch status %d", res.StatusCode)
	}
	res.Body.Close()

	badBaud, err := http.NewRequest(http.MethodPut, ts.URL+"/api/lora", strings.NewReader(
		`{"lora":{"baud":1200}}`))
	if err != nil {
		t.Fatal(err)
	}
	badBaud.Header.Set("Content-Type", "application/json")
	res, err = http.DefaultClient.Do(badBaud)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != http.StatusBadRequest {
		t.Fatalf("bad baud status %d", res.StatusCode)
	}
	res.Body.Close()

	badPassphrase, err := http.NewRequest(http.MethodPut, ts.URL+"/api/lora", strings.NewReader(
		`{"lora":{"air_key":"short"}}`))
	if err != nil {
		t.Fatal(err)
	}
	badPassphrase.Header.Set("Content-Type", "application/json")
	res, err = http.DefaultClient.Do(badPassphrase)
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != http.StatusBadRequest {
		t.Fatalf("short air passphrase status %d", res.StatusCode)
	}
	res.Body.Close()

	st, err := http.Get(ts.URL + "/api/status")
	if err != nil {
		t.Fatal(err)
	}
	stBody, _ := io.ReadAll(st.Body)
	st.Body.Close()
	if strings.Contains(string(stBody), "radio_not_connected") == false {
		t.Fatalf("offline status: %s", stBody)
	}

	cfgRes, err := http.Get(ts.URL + "/api/config")
	if err != nil {
		t.Fatal(err)
	}
	cfgBody, _ := io.ReadAll(cfgRes.Body)
	cfgRes.Body.Close()
	if cfgRes.StatusCode != http.StatusServiceUnavailable {
		t.Fatalf("operator /api/config must proxy, got %d %s", cfgRes.StatusCode, cfgBody)
	}
}
