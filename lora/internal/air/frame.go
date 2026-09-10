package air

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
)

const (
	version     = 1
	typeData    = 0
	typeAck     = 1
	headerSize  = 1 + 1 + 4 + 2 + 2 + 2
	crcSize     = 4
	maxAirBytes = 220
)

var crcTable = crc32.MakeTable(crc32.Castagnoli)

// ================================================================================

type frame struct {
	typ    uint8
	reqID  uint32
	fragI  uint16
	fragN  uint16
	payload []byte
}

// ================================================================================

func maxPayload() int {
	return maxAirBytes - headerSize - crcSize
}

// ================================================================================

func encodeFrame(f frame) []byte {
	p := make([]byte, headerSize+len(f.payload)+crcSize)
	p[0] = version
	p[1] = f.typ
	binary.BigEndian.PutUint32(p[2:6], f.reqID)
	binary.BigEndian.PutUint16(p[6:8], f.fragI)
	binary.BigEndian.PutUint16(p[8:10], f.fragN)
	binary.BigEndian.PutUint16(p[10:12], uint16(len(f.payload)))
	copy(p[headerSize:], f.payload)
	sum := crc32.Checksum(p[:headerSize+len(f.payload)], crcTable)
	binary.BigEndian.PutUint32(p[headerSize+len(f.payload):], sum)
	return p
}

// ================================================================================

func decodeFrame(p []byte) (frame, error) {
	if len(p) < headerSize+crcSize {
		return frame{}, errFrame("short frame")
	}
	if p[0] != version {
		return frame{}, errFrame("bad version")
	}
	plen := int(binary.BigEndian.Uint16(p[10:12]))
	if len(p) != headerSize+plen+crcSize {
		return frame{}, errFrame("length mismatch")
	}
	want := crc32.Checksum(p[:headerSize+plen], crcTable)
	got := binary.BigEndian.Uint32(p[headerSize+plen:])
	if want != got {
		return frame{}, errFrame("crc")
	}
	payload := make([]byte, plen)
	copy(payload, p[headerSize:headerSize+plen])
	return frame{
		typ:     p[1],
		reqID:   binary.BigEndian.Uint32(p[2:6]),
		fragI:   binary.BigEndian.Uint16(p[6:8]),
		fragN:   binary.BigEndian.Uint16(p[8:10]),
		payload: payload,
	}, nil
}

// ================================================================================

func fragment(reqID uint32, blob []byte) []frame {
	n := maxPayload()
	if n < 1 {
		n = 1
	}
	if len(blob) == 0 {
		return []frame{{typ: typeData, reqID: reqID, fragI: 0, fragN: 1}}
	}
	count := (len(blob) + n - 1) / n
	out := make([]frame, 0, count)
	for i := 0; i < count; i++ {
		start := i * n
		end := start + n
		if end > len(blob) {
			end = len(blob)
		}
		out = append(out, frame{
			typ:     typeData,
			reqID:   reqID,
			fragI:   uint16(i),
			fragN:   uint16(count),
			payload: blob[start:end],
		})
	}
	return out
}

// ================================================================================

func errFrame(msg string) error {
	return fmt.Errorf("air frame: %s", msg)
}
