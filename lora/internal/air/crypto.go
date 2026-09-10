package air

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/sha256"
	"fmt"
	"io"
	"strings"
)

const (
	keySize           = 32
	nonceSize         = 12
	minPassphraseSize = 8
	maxPassphraseSize = 128
	keyDomain         = "doggy-lora-air-v1\x00"
)

// ================================================================================

func DeriveKey(passphrase string) ([]byte, error) {
	if strings.ContainsAny(passphrase, "\r\n") {
		return nil, fmt.Errorf("air passphrase must not contain a newline")
	}
	normalized := strings.Trim(passphrase, " \t\f\v")
	if normalized == "" {
		return nil, fmt.Errorf("air passphrase missing")
	}
	if len(normalized) < minPassphraseSize || len(normalized) > maxPassphraseSize {
		return nil, fmt.Errorf("air passphrase must be 8 to 128 bytes")
	}
	sum := sha256.Sum256([]byte(keyDomain + normalized))
	return sum[:], nil
}

// ================================================================================

func KeyOK(passphrase string) bool {
	if strings.ContainsAny(passphrase, "\r\n") {
		return false
	}
	if passphrase == "" {
		return true
	}
	_, err := DeriveKey(passphrase)
	return err == nil
}

// ================================================================================

func seal(key, plain []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	nonce := make([]byte, nonceSize)
	if _, err := io.ReadFull(rand.Reader, nonce); err != nil {
		return nil, err
	}
	return gcm.Seal(nonce, nonce, plain, nil), nil
}

// ================================================================================

func open(key, blob []byte) ([]byte, error) {
	if len(blob) < nonceSize {
		return nil, fmt.Errorf("air crypto: short ciphertext")
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	nonce := blob[:nonceSize]
	return gcm.Open(nil, nonce, blob[nonceSize:], nil)
}
