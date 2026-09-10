package air

// ================================================================================

func cobsEncode(src []byte) []byte {
	if len(src) == 0 {
		return []byte{0x01, 0x00}
	}
	out := make([]byte, 0, len(src)+2+len(src)/254)
	codeIndex := 0
	out = append(out, 0)
	code := byte(1)
	for _, b := range src {
		if b == 0 {
			out[codeIndex] = code
			codeIndex = len(out)
			out = append(out, 0)
			code = 1
			continue
		}
		out = append(out, b)
		code++
		if code == 0xff {
			out[codeIndex] = code
			codeIndex = len(out)
			out = append(out, 0)
			code = 1
		}
	}
	out[codeIndex] = code
	out = append(out, 0)
	return out
}

// ================================================================================

func cobsDecode(src []byte) ([]byte, error) {
	if len(src) == 0 {
		return nil, errFrame("empty COBS")
	}
	if src[len(src)-1] != 0 {
		return nil, errFrame("COBS missing delimiter")
	}
	src = src[:len(src)-1]
	out := make([]byte, 0, len(src))
	for len(src) > 0 {
		code := src[0]
		if code == 0 {
			return nil, errFrame("COBS zero code")
		}
		src = src[1:]
		n := int(code) - 1
		if n > len(src) {
			return nil, errFrame("COBS truncated")
		}
		out = append(out, src[:n]...)
		src = src[n:]
		if code != 0xff && len(src) > 0 {
			out = append(out, 0)
		}
	}
	return out, nil
}
