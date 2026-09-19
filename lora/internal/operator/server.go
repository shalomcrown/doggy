package operator

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"doggy-lora/internal/air"
	"doggy-lora/internal/hop"
	"doggy-lora/internal/pb"
	"doggy-lora/internal/settings"
	"log"

	"google.golang.org/protobuf/proto"
)

// ================================================================================

type roundTripper interface {
	RoundTrip(context.Context, *pb.AirRequest) (*pb.AirResponse, error)
}

// ================================================================================

type Server struct {
	webRoot   string
	localFile string
	pages     *http.ServeMux
	rt        roundTripper

	statusMu   sync.Mutex
	statusBody []byte
	statusCT   string
	statusCode int
	statusAt   time.Time
}

// ================================================================================

func New(webRoot, localFile string, rt *hop.Runtime) http.Handler {
	var channel roundTripper
	if rt != nil {
		channel = rt
	}
	s := &Server{
		webRoot:   webRoot,
		localFile: localFile,
		pages:     http.NewServeMux(),
		rt:        channel,
	}
	s.pages.HandleFunc("/", s.serveRoot)
	s.pages.HandleFunc("/lora", s.serveLoraPage)
	s.pages.HandleFunc("/dog", s.serveFile("index.html"))
	s.pages.HandleFunc("/rover", s.serveFile("rover.html"))
	return s
}

// ================================================================================

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if strings.HasPrefix(r.URL.Path, "/api/") {
		if r.URL.Path == "/api/lora" {
			s.handleLora(w, r)
			return
		}
		s.proxy(w, r)
		return
	}
	s.pages.ServeHTTP(w, r)
}

// ================================================================================

func (s *Server) serveRoot(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	s.serveLoraPage(w, r)
}

// ================================================================================

func (s *Server) serveLoraPage(w http.ResponseWriter, r *http.Request) {
	p := filepath.Join(s.webRoot, "lora.html")
	http.ServeFile(w, r, p)
}

// ================================================================================

func (s *Server) serveFile(name string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, filepath.Join(s.webRoot, name))
	}
}

// ================================================================================

func (s *Server) handleLora(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		cur, err := settings.LoadFile(s.localFile)
		if os.IsNotExist(err) {
			cur = settings.Default()
		} else if err != nil {
			http.Error(w, `{"error":"config_read"}`, http.StatusInternalServerError)
			return
		} else {
			cur = cur.Normalize()
		}
		writeJSON(w, map[string]any{"lora": cur.Public()})
	case http.MethodPut:
		body := json.NewDecoder(r.Body)
		var wrap struct {
			Lora settings.Settings `json:"lora"`
		}
		if err := body.Decode(&wrap); err != nil {
			http.Error(w, `{"error":"bad_json"}`, http.StatusBadRequest)
			return
		}
		if air.KeyOK(wrap.Lora.AirKey) == false {
			http.Error(w, `{"error":"bad_json"}`, http.StatusBadRequest)
			return
		}
		cur := wrap.Lora.Normalize()
		if cur.ChannelsOK() == false || cur.BaudOK() == false || cur.DeviceOK() == false {
			http.Error(w, `{"error":"bad_json"}`, http.StatusBadRequest)
			return
		}
		prev, err := settings.LoadFile(s.localFile)
		if err == nil && cur.AirKey == "" {
			cur.AirKey = prev.AirKey
		}
		if err := os.MkdirAll(filepath.Dir(s.localFile), 0755); err != nil {
			http.Error(w, `{"error":"config_write"}`, http.StatusInternalServerError)
			return
		}
		if err := settings.SaveFile(s.localFile, cur); err != nil {
			http.Error(w, `{"error":"config_write"}`, http.StatusInternalServerError)
			return
		}
		writeJSON(w, map[string]any{"lora": cur.Public()})
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

// ================================================================================

func (s *Server) proxy(w http.ResponseWriter, r *http.Request) {
	if s.rt == nil {
		writeOffline(w)
		return
	}
	if r.Method == http.MethodGet && r.URL.Path == "/api/status" {
		s.proxyStatus(w, r)
		return
	}
	s.roundTripWrite(w, r, 20*time.Second)
}

// ================================================================================

func (s *Server) proxyStatus(w http.ResponseWriter, r *http.Request) {
	s.statusMu.Lock()
	if time.Since(s.statusAt) < time.Second && len(s.statusBody) > 0 {
		code := s.statusCode
		ct := s.statusCT
		body := append([]byte(nil), s.statusBody...)
		s.statusMu.Unlock()
		if ct != "" {
			w.Header().Set("Content-Type", ct)
		}
		w.WriteHeader(code)
		_, _ = w.Write(body)
		return
	}
	s.statusMu.Unlock()

	resp, err := s.roundTrip(r, 8*time.Second)
	if err != nil {
		// Log and return a more descriptive JSON error to help operator debugging
		log.Printf("operator: roundTrip /api/status error: %v", err)
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusServiceUnavailable)
		_, _ = w.Write([]byte(`{"error":"radio_not_connected","message":"` + err.Error() + `"}`))
		return
	}
	code, ct, body := air.AirHTTP(resp)
	s.statusMu.Lock()
	s.statusBody = append([]byte(nil), body...)
	s.statusCT = ct
	s.statusCode = code
	s.statusAt = time.Now()
	s.statusMu.Unlock()
	writeAir(w, resp)
}

// ================================================================================

func (s *Server) roundTripWrite(w http.ResponseWriter, r *http.Request, timeout time.Duration) {
	resp, err := s.roundTrip(r, timeout)
	if err != nil {
		log.Printf("operator: roundTrip error %v for %s %s", err, r.Method, r.URL.Path)
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusServiceUnavailable)
		_, _ = w.Write([]byte(`{"error":"radio_not_connected","message":"` + err.Error() + `"}`))
		return
	}
	writeAir(w, resp)
}

// ================================================================================

func (s *Server) roundTrip(r *http.Request, timeout time.Duration) (*pb.AirResponse, error) {
	body, err := io.ReadAll(io.LimitReader(r.Body, 48*1024))
	if err != nil {
		return nil, err
	}
	airReq, err := air.HTTPToAir(r.Method, r.URL.Path, body)
	if err != nil {
		return &pb.AirResponse{
			Status: proto.Int32(http.StatusBadRequest),
			Payload: &pb.AirResponse_Error{
				Error: &pb.ApiError{Error: proto.String("bad_request")},
			},
		}, nil
	}
	ctx, cancel := context.WithTimeout(r.Context(), timeout)
	defer cancel()
	return s.rt.RoundTrip(ctx, airReq)
}

// ================================================================================

func writeAir(w http.ResponseWriter, resp *pb.AirResponse) {
	code, ct, body := air.AirHTTP(resp)
	if ct != "" {
		w.Header().Set("Content-Type", ct)
	}
	w.WriteHeader(code)
	_, _ = w.Write(body)
}

// ================================================================================

func writeOffline(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusServiceUnavailable)
	_, _ = w.Write([]byte(`{"error":"radio_not_connected","message":"LoRa radio not connected"}`))
}

// ================================================================================

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}
