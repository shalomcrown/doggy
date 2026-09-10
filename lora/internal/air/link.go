package air

import (
	"bufio"
	"io"
	"sync"
	"time"
)

// ================================================================================

type deadliner interface {
	SetReadDeadline(time.Time) error
}

// ================================================================================

type Link struct {
	mu sync.Mutex
	w  io.Writer
	r  *bufio.Reader
	rd deadliner
}

// ================================================================================

func NewLink(rw io.ReadWriter) *Link {
	l := &Link{w: rw, r: bufio.NewReader(rw)}
	if d, ok := rw.(deadliner); ok {
		l.rd = d
	}
	return l
}

// ================================================================================

func (l *Link) send(f frame) error {
	blob := cobsEncode(encodeFrame(f))
	l.mu.Lock()
	defer l.mu.Unlock()
	_, err := l.w.Write(blob)
	return err
}

// ================================================================================

func (l *Link) recv(deadline time.Time) (frame, error) {
	if l.rd != nil {
		if err := l.rd.SetReadDeadline(deadline); err != nil {
			return frame{}, err
		}
	}
	raw, err := readDelimited(l.r)
	if err != nil {
		return frame{}, err
	}
	decoded, err := cobsDecode(raw)
	if err != nil {
		return frame{}, err
	}
	return decodeFrame(decoded)
}

// ================================================================================

func readDelimited(r *bufio.Reader) ([]byte, error) {
	var buf []byte
	for {
		b, err := r.ReadByte()
		if err != nil {
			return nil, err
		}
		buf = append(buf, b)
		if b == 0 {
			return buf, nil
		}
		if len(buf) > 4096 {
			return nil, errFrame("COBS overflow")
		}
	}
}
