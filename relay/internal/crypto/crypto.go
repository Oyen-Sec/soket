// Project: Soket.io V7.0
// Module: C2 Component
// Description: Professional C2 Infrastructure component.


package crypto

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/subtle"
	"encoding/base64"
	"fmt"
	"io"
	"os"
	"strings"

	"golang.org/x/crypto/blake2b"
)

// ... (constants)

func LoadSignifyPrivateKey(path string) (ed25519.PrivateKey, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	lines := strings.Split(string(data), "\n")
	if len(lines) < 2 {
		return nil, fmt.Errorf("invalid signify key format")
	}

	keyData, err := base64.StdEncoding.DecodeString(lines[1])
	if err != nil {
		return nil, err
	}

	// Signify Ed25519 secret key format:
	// 2 bytes magic ("Ed"), 2 bytes KDF magic ("BK"), 4 bytes KDF rounds,
	// 8 bytes salt, 8 bytes key ID, 64 bytes secret key.
	// Total: 2+2+4+8+8+64 = 88 bytes.
	if len(keyData) < 88 {
		return nil, fmt.Errorf("secret key too short")
	}

	return ed25519.PrivateKey(keyData[len(keyData)-64:]), nil
}

func LoadSignifyPublicKey(path string) (ed25519.PublicKey, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	lines := strings.Split(string(data), "\n")
	if len(lines) < 2 {
		return nil, fmt.Errorf("invalid signify key format")
	}

	keyData, err := base64.StdEncoding.DecodeString(lines[1])
	if err != nil {
		return nil, err
	}

	// Signify Ed25519 public key format:
	// 2 bytes magic ("Ed"), 2 bytes alg ("Tm"), 8 bytes key ID, 32 bytes public key.
	// Total: 2+2+8+32 = 44 bytes.
	// (Note: some versions might have 42 bytes if key ID is 4 bytes, but our hex showed 42 bytes base64? No, 56 characters = 42 bytes)
	// Let's check the length again.
	if len(keyData) < 32 {
		return nil, fmt.Errorf("public key too short")
	}

	return ed25519.PublicKey(keyData[len(keyData)-32:]), nil
}

const (
	KeySize         = 32
	NonceSize       = 24
	MACSize         = 16
	HashSize        = 64
	FingerprintSize = 16
	Ed25519SecretSize = 64
	Ed25519PublicSize = 32
	Ed25519SigSize    = 64
)

func Blake2bHash(data []byte, hashLen int) ([]byte, error) {
	if hashLen <= 0 || hashLen > 64 {
		return nil, fmt.Errorf("invalid hash length: %d", hashLen)
	}

	h, err := blake2b.New(hashLen, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create BLAKE2b: %w", err)
	}

	if _, err := h.Write(data); err != nil {
		return nil, fmt.Errorf("failed to hash data: %w", err)
	}

	return h.Sum(nil), nil
}

func Blake2bHashKeyed(data []byte, key []byte, hashLen int) ([]byte, error) {
	if hashLen <= 0 || hashLen > 64 {
		return nil, fmt.Errorf("invalid hash length: %d", hashLen)
	}

	h, err := blake2b.New(hashLen, key)
	if err != nil {
		return nil, fmt.Errorf("failed to create keyed BLAKE2b: %w", err)
	}

	if _, err := h.Write(data); err != nil {
		return nil, fmt.Errorf("failed to hash data: %w", err)
	}

	return h.Sum(nil), nil
}

func SecureRandom(buffer []byte) error {
	_, err := io.ReadFull(rand.Reader, buffer)
	return err
}

func ConstantTimeCompare(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	return subtle.ConstantTimeCompare(a, b) == 1
}

func Wipe(secret []byte) {
	for i := range secret {
		secret[i] = 0
	}
}

func DeriveSessionKey(sharedSecret []byte) ([]byte, error) {

	return Blake2bHash(sharedSecret, KeySize)
}

func ComputeFingerprint(sharedSecret []byte) ([]byte, error) {
	return Blake2bHash(sharedSecret, FingerprintSize)
}
