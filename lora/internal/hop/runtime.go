package hop

import (
	"context"
	"crypto/tls"
	"fmt"
	"io"
	"log"
	"net/http"
	"sync"
	"time"

	"doggy-lora/internal/air"
	"doggy-lora/internal/pb"
	"doggy-lora/internal/radio"
	"doggy-lora/internal/settings"
)

// ================================================================================

type Role int

const (
	RoleRobot Role = iota
	RoleOperator
)

// ================================================================================

type Runtime struct {
	mu         sync.Mutex
	role       Role
	dev        *radio.Device
	client     *air.Client
	server     *air.Server
	cancel     context.CancelFunc
	passphrase string
	base       string
	http       *http.Client
}

// ================================================================================

func New(role Role, dev *radio.Device, robotBase string) *Runtime {
	rt := &Runtime{role: role, dev: dev, base: robotBase}
	if role == RoleRobot {
		rt.http = &http.Client{
			Timeout: 15 * time.Second,
			Transport: &http.Transport{
				TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
			},
		}
		if rt.base == "" {
			rt.base = "https://127.0.0.1"
		}
	}
	return rt
}

// ================================================================================

func (rt *Runtime) Sync(desired settings.Settings) {
	desired = desired.Normalize()
	rt.mu.Lock()
	radioSame := settings.Equal(rt.dev.Applied(), desired)
	keySame := rt.passphrase == desired.AirKey
	airUp := rt.client != nil || rt.server != nil
	rt.mu.Unlock()

	if radioSame && keySame && airUp {
		return
	}

	rt.stopAir()
	rt.dev.Apply(desired)

	rt.mu.Lock()
	rt.passphrase = desired.AirKey
	rt.mu.Unlock()

	rt.startAir(desired)
}

// ================================================================================

func (rt *Runtime) stopAir() {
	rt.mu.Lock()
	cancel := rt.cancel
	c := rt.client
	srv := rt.server
	rt.cancel = nil
	rt.client = nil
	rt.server = nil
	rt.mu.Unlock()
	if cancel != nil {
		cancel()
	}
	if c != nil {
		c.Close()
	}
	if srv != nil {
		srv.Close()
	}
}

// ================================================================================

func (rt *Runtime) startAir(desired settings.Settings) {
	if desired.Enabled == false {
		return
	}
	rw := rt.dev.ReadWriter()
	if rw == nil {
		return
	}
	key, err := air.DeriveKey(desired.AirKey)
	if err != nil {
		log.Printf("lora air hop idle until a valid air passphrase is set")
		return
	}
	ctx, cancel := context.WithCancel(context.Background())
	rt.mu.Lock()
	rt.cancel = cancel
	if rt.role == RoleOperator {
		rt.client = air.NewClient(rw, key)
		rt.mu.Unlock()
		return
	}
	srv := air.NewServer(rw, key)
	rt.server = srv
	httpClient := rt.http
	base := rt.base
	rt.mu.Unlock()
	go func() {
		_ = srv.Serve(ctx, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			body, _ := io.ReadAll(io.LimitReader(r.Body, 48*1024))
			resp := air.ForwardLocal(r.Context(), httpClient, base, air.HttpRequest{
				Method:      r.Method,
				Path:        requestPath(r),
				ContentType: r.Header.Get("Content-Type"),
				Body:        body,
			})
			if resp.ContentType != "" {
				w.Header().Set("Content-Type", resp.ContentType)
			}
			w.WriteHeader(resp.Status)
			_, _ = w.Write(resp.Body)
		}))
	}()
}

// ================================================================================

func requestPath(r *http.Request) string {
	p := r.URL.Path
	if r.URL.RawQuery != "" {
		p += "?" + r.URL.RawQuery
	}
	return p
}

// ================================================================================

func (rt *Runtime) RoundTrip(ctx context.Context, req *pb.AirRequest) (*pb.AirResponse, error) {
	rt.mu.Lock()
	c := rt.client
	rt.mu.Unlock()
	if c == nil {
		return nil, fmt.Errorf("radio_not_connected")
	}
	return c.RoundTrip(ctx, req)
}
