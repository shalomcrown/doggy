package air

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"doggy-lora/internal/pb"

	"google.golang.org/protobuf/proto"
)

const (
	ackWait     = 1500 * time.Millisecond
	maxRetries  = 3
	maxReqBytes = 48 * 1024
	maxCache    = 32
)

// ================================================================================

type assembled struct {
	reqID uint32
	plain []byte
}

// ================================================================================

type engine struct {
	link *Link
	key  []byte

	mu      sync.Mutex
	pending map[ackKey]chan struct{}
	inbox   map[uint32]*reassembly

	complete chan assembled
	stop     chan struct{}
	stopped  sync.Once
	done     sync.WaitGroup
}

// ================================================================================

type ackKey struct {
	reqID uint32
	fragI uint16
}

// ================================================================================

type reassembly struct {
	n     uint16
	parts [][]byte
	got   int
}

// ================================================================================

func newEngine(rw io.ReadWriter, key []byte) *engine {
	e := &engine{
		link:     NewLink(rw),
		key:      append([]byte(nil), key...),
		pending:  make(map[ackKey]chan struct{}),
		inbox:    make(map[uint32]*reassembly),
		complete: make(chan assembled, 8),
		stop:     make(chan struct{}),
	}
	e.done.Add(1)
	go e.readLoop()
	return e
}

// ================================================================================

func (e *engine) Close() {
	e.stopped.Do(func() {
		close(e.stop)
		if e.link != nil && e.link.rd != nil {
			_ = e.link.rd.SetReadDeadline(time.Now())
		}
	})
	e.done.Wait()
}

// ================================================================================

func (e *engine) readLoop() {
	defer e.done.Done()
	for {
		select {
		case <-e.stop:
			return
		default:
		}
		f, err := e.link.recv(time.Now().Add(2 * time.Second))
		if err != nil {
			select {
			case <-e.stop:
				return
			default:
			}
			continue
		}
		if f.typ == typeAck {
			e.signalAck(f)
			continue
		}
		if f.typ != typeData {
			continue
		}
		_ = e.link.send(frame{typ: typeAck, reqID: f.reqID, fragI: f.fragI, fragN: f.fragN})
		blob, ready := e.collect(f)
		if ready == false {
			continue
		}
		plain, err := open(e.key, blob)
		if err != nil {
			continue
		}
		select {
		case e.complete <- assembled{reqID: f.reqID, plain: plain}:
		case <-e.stop:
			return
		}
	}
}

// ================================================================================

func (e *engine) signalAck(f frame) {
	e.mu.Lock()
	ch, ok := e.pending[ackKey{reqID: f.reqID, fragI: f.fragI}]
	if ok {
		delete(e.pending, ackKey{reqID: f.reqID, fragI: f.fragI})
	}
	e.mu.Unlock()
	if ok == false {
		return
	}
	select {
	case ch <- struct{}{}:
	default:
	}
}

// ================================================================================

func (e *engine) collect(f frame) ([]byte, bool) {
	e.mu.Lock()
	defer e.mu.Unlock()
	asm, ok := e.inbox[f.reqID]
	if ok == false {
		if f.fragN == 0 {
			return nil, false
		}
		asm = &reassembly{n: f.fragN, parts: make([][]byte, f.fragN)}
		e.inbox[f.reqID] = asm
	}
	if int(f.fragI) >= len(asm.parts) {
		return nil, false
	}
	if asm.parts[f.fragI] == nil {
		asm.parts[f.fragI] = f.payload
		asm.got++
	}
	if asm.got < int(asm.n) {
		return nil, false
	}
	delete(e.inbox, f.reqID)
	return bytes.Join(asm.parts, nil), true
}

// ================================================================================

func (e *engine) sendReliable(ctx context.Context, f frame) error {
	key := ackKey{reqID: f.reqID, fragI: f.fragI}
	for attempt := 0; attempt < maxRetries; attempt++ {
		wait := make(chan struct{}, 1)
		e.mu.Lock()
		e.pending[key] = wait
		e.mu.Unlock()
		if err := e.link.send(f); err != nil {
			return err
		}
		timer := time.NewTimer(ackWait)
		select {
		case <-ctx.Done():
			timer.Stop()
			return ctx.Err()
		case <-e.stop:
			timer.Stop()
			return fmt.Errorf("air: closed")
		case <-wait:
			timer.Stop()
			return nil
		case <-timer.C:
			e.mu.Lock()
			delete(e.pending, key)
			e.mu.Unlock()
		}
	}
	return fmt.Errorf("air: no ACK for req %d frag %d", f.reqID, f.fragI)
}

// ================================================================================

func (e *engine) sendBlob(ctx context.Context, reqID uint32, blob []byte) error {
	frames := fragment(reqID, blob)
	for i := range frames {
		if err := e.sendReliable(ctx, frames[i]); err != nil {
			return err
		}
	}
	return nil
}

// ================================================================================

type Client struct {
	e      *engine
	nextID uint32
}

// ================================================================================

func NewClient(rw io.ReadWriter, key []byte) *Client {
	return &Client{e: newEngine(rw, key)}
}

// ================================================================================

func (c *Client) Close() { c.e.Close() }

// ================================================================================

func (c *Client) RoundTrip(ctx context.Context, req *pb.AirRequest) (*pb.AirResponse, error) {
	plain, err := proto.Marshal(req)
	if err != nil {
		return nil, err
	}
	blob, err := seal(c.e.key, plain)
	if err != nil {
		return nil, err
	}
	id := atomic.AddUint32(&c.nextID, 1)
	if err := c.e.sendBlob(ctx, id, blob); err != nil {
		return nil, err
	}
	for {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-c.e.stop:
			return nil, fmt.Errorf("air: closed")
		case msg := <-c.e.complete:
			if msg.reqID != id {
				continue
			}
			out := &pb.AirResponse{}
			if err := proto.Unmarshal(msg.plain, out); err != nil {
				return nil, err
			}
			return out, nil
		}
	}
}

// ================================================================================

type Server struct {
	e     *engine
	cache map[uint32]*pb.AirResponse
	ord   []uint32
	mu    sync.Mutex
}

// ================================================================================

func NewServer(rw io.ReadWriter, key []byte) *Server {
	return &Server{e: newEngine(rw, key), cache: make(map[uint32]*pb.AirResponse)}
}

// ================================================================================

func (s *Server) Close() { s.e.Close() }

// ================================================================================

func (s *Server) Serve(ctx context.Context, h http.Handler) error {
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-s.e.stop:
			return nil
		case msg := <-s.e.complete:
			req := &pb.AirRequest{}
			if err := proto.Unmarshal(msg.plain, req); err != nil {
				continue
			}
			resp := s.cachedOrDispatch(msg.reqID, req, h)
			plain, err := proto.Marshal(resp)
			if err != nil {
				return err
			}
			blob, err := seal(s.e.key, plain)
			if err != nil {
				return err
			}
			if err := s.e.sendBlob(ctx, msg.reqID, blob); err != nil {
				return err
			}
		}
	}
}

// ================================================================================

func (s *Server) cachedOrDispatch(reqID uint32, req *pb.AirRequest, h http.Handler) *pb.AirResponse {
	s.mu.Lock()
	if cached, hit := s.cache[reqID]; hit {
		s.mu.Unlock()
		return proto.Clone(cached).(*pb.AirResponse)
	}
	s.mu.Unlock()
	httpReq, err := AirToHTTP(req)
	var httpResp HttpResponse
	if err != nil {
		httpResp = HttpResponse{
			Status:      http.StatusBadRequest,
			ContentType: "application/json",
			Body:        []byte(`{"error":"bad_request"}`),
		}
	} else {
		httpResp = dispatch(h, httpReq)
	}
	resp := HTTPToAirResponse(req, httpResp)
	s.mu.Lock()
	s.cache[reqID] = proto.Clone(resp).(*pb.AirResponse)
	s.ord = append(s.ord, reqID)
	if len(s.ord) > maxCache {
		old := s.ord[0]
		s.ord = s.ord[1:]
		delete(s.cache, old)
	}
	s.mu.Unlock()
	return resp
}

// ================================================================================

func dispatch(h http.Handler, req HttpRequest) HttpResponse {
	if allowedPath(req.Path) == false {
		return HttpResponse{Status: http.StatusForbidden, ContentType: "application/json", Body: []byte(`{"error":"forbidden_path"}`)}
	}
	if len(req.Body) > maxReqBytes {
		return HttpResponse{Status: http.StatusRequestEntityTooLarge, ContentType: "application/json", Body: []byte(`{"error":"too_large"}`)}
	}
	httpReq := httptest.NewRequest(req.Method, req.Path, bytes.NewReader(req.Body))
	if req.ContentType != "" {
		httpReq.Header.Set("Content-Type", req.ContentType)
	}
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httpReq)
	return HttpResponse{Status: rr.Code, ContentType: rr.Header().Get("Content-Type"), Body: rr.Body.Bytes()}
}

// ================================================================================

func allowedPath(path string) bool {
	if strings.HasPrefix(path, "/api/") == false {
		return false
	}
	if strings.Contains(path, "..") {
		return false
	}
	return true
}

// ================================================================================

func ForwardLocal(ctx context.Context, client *http.Client, base string, req HttpRequest) HttpResponse {
	if allowedPath(req.Path) == false {
		return HttpResponse{Status: http.StatusForbidden, ContentType: "application/json", Body: []byte(`{"error":"forbidden_path"}`)}
	}
	url := strings.TrimRight(base, "/") + req.Path
	httpReq, err := http.NewRequestWithContext(ctx, req.Method, url, bytes.NewReader(req.Body))
	if err != nil {
		return HttpResponse{Status: 502, ContentType: "application/json", Body: []byte(`{"error":"bad_request"}`)}
	}
	if req.ContentType != "" {
		httpReq.Header.Set("Content-Type", req.ContentType)
	}
	res, err := client.Do(httpReq)
	if err != nil {
		return HttpResponse{Status: 502, ContentType: "application/json", Body: []byte(`{"error":"robot_http"}`)}
	}
	defer res.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(res.Body, maxReqBytes))
	return HttpResponse{Status: res.StatusCode, ContentType: res.Header.Get("Content-Type"), Body: body}
}
