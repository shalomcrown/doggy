package air

import (
	"bytes"
	"encoding/hex"
	"strings"
	"testing"

	"doggy-lora/internal/pb"

	"google.golang.org/protobuf/proto"
)

// ================================================================================

func TestCobsRoundTrip(t *testing.T) {
	cases := [][]byte{
		{},
		{0},
		{1, 2, 3},
		{0, 0, 1},
		bytes.Repeat([]byte{1}, 300),
	}
	for _, in := range cases {
		got, err := cobsDecode(cobsEncode(in))
		if err != nil {
			t.Fatalf("decode: %v", err)
		}
		if bytes.Equal(got, in) == false {
			t.Fatalf("roundtrip %v -> %v", in, got)
		}
	}
}

// ================================================================================

func TestFrameCrcRejects(t *testing.T) {
	f := encodeFrame(frame{typ: typeData, reqID: 1, fragI: 0, fragN: 1, payload: []byte("hi")})
	f[4] ^= 0xff
	if _, err := decodeFrame(f); err == nil {
		t.Fatal("expected crc error")
	}
}

// ================================================================================

func TestCryptoRoundTrip(t *testing.T) {
	key := bytes.Repeat([]byte{0x11}, 32)
	blob, err := seal(key, []byte("hello"))
	if err != nil {
		t.Fatal(err)
	}
	plain, err := open(key, blob)
	if err != nil {
		t.Fatal(err)
	}
	if string(plain) != "hello" {
		t.Fatalf("got %q", plain)
	}
	if _, err := open(bytes.Repeat([]byte{0x22}, 32), blob); err == nil {
		t.Fatal("wrong key should fail")
	}
}

// ================================================================================

func TestAirRequestRoundTrip(t *testing.T) {
	req, err := HTTPToAir("PUT", "/api/config", []byte(`{"robot":{"type":"ROVER"}}`))
	if err != nil {
		t.Fatal(err)
	}
	plain, err := proto.Marshal(req)
	if err != nil {
		t.Fatal(err)
	}
	got := &pb.AirRequest{}
	if err := proto.Unmarshal(plain, got); err != nil {
		t.Fatal(err)
	}
	if got.GetPutConfig().GetRobot().GetType() != pb.RobotType_ROVER {
		t.Fatalf("%+v", got)
	}
}

// ================================================================================

func TestHeartbeatRequestRoundTrip(t *testing.T) {
	req, err := HTTPToAir("POST", "/api/heartbeat", nil)
	if err != nil {
		t.Fatal(err)
	}
	if req.GetHeartbeat() == nil {
		t.Fatal("HTTP heartbeat did not map to an air heartbeat")
	}
	httpReq, err := AirToHTTP(req)
	if err != nil {
		t.Fatal(err)
	}
	if httpReq.Method != "POST" || httpReq.Path != "/api/heartbeat" {
		t.Fatalf("heartbeat mapped to %s %s", httpReq.Method, httpReq.Path)
	}
}

// ================================================================================

func TestDeriveKey(t *testing.T) {
	if KeyOK("") == false {
		t.Fatal("empty ok")
	}
	if KeyOK("short") {
		t.Fatal("short passphrase")
	}
	if KeyOK(strings.Repeat("a", 8)) == false {
		t.Fatal("8-byte passphrase")
	}
	if KeyOK(strings.Repeat("a", 128)) == false {
		t.Fatal("128-byte passphrase")
	}
	if KeyOK("        ") {
		t.Fatal("whitespace-only passphrase")
	}
	if KeyOK(strings.Repeat("a", 129)) {
		t.Fatal("long passphrase")
	}
	if KeyOK("valid passphrase\n") {
		t.Fatal("newline")
	}
	if KeyOK("valid passphrase\r") {
		t.Fatal("carriage return")
	}

	key, err := DeriveKey("correct horse battery staple")
	if err != nil {
		t.Fatal(err)
	}
	if len(key) != keySize {
		t.Fatalf("key length %d", len(key))
	}
	if got := hex.EncodeToString(key); got != "61f7f8b65d0dbacb19d029c98c48e44bea985ded2422ca3c18db02c436121a78" {
		t.Fatalf("derived key %s", got)
	}

	hexLooking := strings.Repeat("ab", 32)
	key, err = DeriveKey(hexLooking)
	if err != nil {
		t.Fatal(err)
	}
	if got := hex.EncodeToString(key); got != "af87ff3fb6b8a3daacbcf7fc85c49fa1ff9b57f4a68ae06ceb70c4dcc02fd91f" {
		t.Fatalf("hex-looking passphrase derived key %s", got)
	}

	other, err := DeriveKey("different passphrase")
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(key, other) {
		t.Fatal("distinct passphrases derived the same key")
	}
}
