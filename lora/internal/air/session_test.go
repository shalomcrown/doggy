package air

import (
	"context"
	"errors"
	"io"
	"net"
	"net/http"
	"testing"
	"time"

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

// ================================================================================

func TestRetryDelayUsesBoundedExponentialBackoff(t *testing.T) {
	cases := []struct {
		attempt int
		jitter  time.Duration
		want    time.Duration
	}{
		{attempt: 0, jitter: 500 * time.Millisecond, want: 2 * time.Second},
		{attempt: 1, jitter: 500 * time.Millisecond, want: 3500 * time.Millisecond},
		{attempt: 2, jitter: 500 * time.Millisecond, want: 6500 * time.Millisecond},
		{attempt: 20, jitter: 500 * time.Millisecond, want: 6500 * time.Millisecond},
	}
	for _, tc := range cases {
		if got := retryDelay(tc.attempt, tc.jitter); got != tc.want {
			t.Fatalf("attempt %d delay %s, want %s", tc.attempt, got, tc.want)
		}
	}
}

// ================================================================================

type discardReadWriter struct{}

// ================================================================================

func (discardReadWriter) Read([]byte) (int, error) {
	return 0, io.EOF
}

// ================================================================================

func (discardReadWriter) Write(p []byte) (int, error) {
	return len(p), nil
}

// ================================================================================

type countingReadWriter struct {
	writes int
}

// ================================================================================

func (c *countingReadWriter) Read([]byte) (int, error) {
	return 0, io.EOF
}

// ================================================================================

func (c *countingReadWriter) Write(p []byte) (int, error) {
	c.writes++
	return len(p), nil
}

// ================================================================================

func TestReliableSendCancellationInterruptsBackoff(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	rw := &countingReadWriter{}
	e := &engine{
		link:      NewLink(rw),
		pending:   make(map[ackKey]chan struct{}),
		stop:      make(chan struct{}),
		retryWait: func(int) time.Duration { return time.Hour },
	}
	start := time.Now()
	err := e.sendReliable(ctx, frame{typ: typeData, reqID: 1, fragN: 1})
	if errors.Is(err, context.Canceled) == false {
		t.Fatalf("error %v, want context cancellation", err)
	}
	if time.Since(start) > 100*time.Millisecond {
		t.Fatal("cancellation did not interrupt retry backoff promptly")
	}
	if rw.writes != 0 {
		t.Fatalf("canceled send wrote %d frames", rw.writes)
	}
}

// ================================================================================

func TestReliableSendUsesEveryBackoffAttempt(t *testing.T) {
	var attempts []int
	e := &engine{
		link:    NewLink(discardReadWriter{}),
		pending: make(map[ackKey]chan struct{}),
		stop:    make(chan struct{}),
		retryWait: func(attempt int) time.Duration {
			attempts = append(attempts, attempt)
			return time.Millisecond
		},
	}
	err := e.sendReliable(context.Background(), frame{typ: typeData, reqID: 1, fragN: 1})
	if err == nil {
		t.Fatal("missing ACK must fail after all retries")
	}
	if len(attempts) != maxAttempts || attempts[0] != 0 || attempts[1] != 1 || attempts[2] != 2 {
		t.Fatalf("backoff attempts %v, want [0 1 2]", attempts)
	}
	if len(e.pending) != 0 {
		t.Fatalf("pending ACK registrations leaked: %v", e.pending)
	}
}

// ================================================================================

func TestReliableSendShutdownInterruptsBackoff(t *testing.T) {
	e := &engine{
		link:      NewLink(discardReadWriter{}),
		pending:   make(map[ackKey]chan struct{}),
		stop:      make(chan struct{}),
		retryWait: func(int) time.Duration { return time.Hour },
	}
	close(e.stop)
	start := time.Now()
	err := e.sendReliable(context.Background(), frame{typ: typeData, reqID: 1, fragN: 1})
	if err == nil || err.Error() != "air: closed" {
		t.Fatalf("error %v, want closed engine", err)
	}
	if time.Since(start) > 100*time.Millisecond {
		t.Fatal("shutdown did not interrupt retry backoff promptly")
	}
}
