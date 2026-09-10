package air

import (
	"context"
	"io"
	"net"
	"net/http"
	"testing"

	"doggy-lora/internal/pb"

	"google.golang.org/protobuf/proto"
)

// ================================================================================

func TestRoundTripOverPipe(t *testing.T) {
	a, b := net.Pipe()
	defer a.Close()
	defer b.Close()
	key := make([]byte, 32)
	for i := range key {
		key[i] = byte(i)
	}
	cli := NewClient(a, key)
	srv := NewServer(b, key)
	defer cli.Close()
	defer srv.Close()

	mux := http.NewServeMux()
	mux.HandleFunc("/api/status", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = io.WriteString(w, `{"type":"DOG"}`)
	})
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() { _ = srv.Serve(ctx, mux) }()

	got, err := cli.RoundTrip(context.Background(), &pb.AirRequest{
		Op: &pb.AirRequest_GetStatus{GetStatus: &pb.Empty{}},
	})
	if err != nil {
		t.Fatal(err)
	}
	if got.GetStatus() != 200 || got.GetStatusBody().GetType() != pb.RobotType_DOG {
		t.Fatalf("%+v", got)
	}
}

// ================================================================================

func TestForbiddenPath(t *testing.T) {
	resp := dispatch(http.NotFoundHandler(), HttpRequest{Method: "GET", Path: "/etc/passwd"})
	if resp.Status != http.StatusForbidden {
		t.Fatalf("status %d", resp.Status)
	}
}

// ================================================================================

func TestIdempotentCache(t *testing.T) {
	n := 0
	h := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		n++
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		_, _ = io.WriteString(w, `{"items":[]}`)
	})
	s := &Server{cache: map[uint32]*pb.AirResponse{}}
	req := &pb.AirRequest{Op: &pb.AirRequest_Home{Home: &pb.Empty{}}}
	r1 := s.cachedOrDispatch(7, req, h)
	r2 := s.cachedOrDispatch(7, proto.Clone(req).(*pb.AirRequest), h)
	if n != 1 {
		t.Fatalf("n=%d", n)
	}
	_, _, body1 := AirHTTP(r1)
	_, _, body2 := AirHTTP(r2)
	if bytesEqual(body1, body2) == false {
		t.Fatalf("%s %s", body1, body2)
	}
}

// ================================================================================

func bytesEqual(a, b []byte) bool {
	return string(a) == string(b)
}
