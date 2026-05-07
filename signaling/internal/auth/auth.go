// Package auth - Advanced cryptographic authentication and identity management for Soket.io signaling.
package auth

import (
	"crypto/ed25519"
	"crypto/rand"
	"fmt"
)

// Identity represents a unique agent identity based on Ed25519.
type Identity struct {
	PublicKey  ed25519.PublicKey
	PrivateKey ed25519.PrivateKey
}

// NewIdentity generates a new cryptographic identity.
func NewIdentity() (*Identity, error) {
	pub, priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, err
	}
	return &Identity{PublicKey: pub, PrivateKey: priv}, nil
}

// Sign generates a signature for the given message using the identity's private key.
func (i *Identity) Sign(message []byte) []byte {
	return ed25519.Sign(i.PrivateKey, message)
}

// Verify checks the validity of a signature against a public key.
func Verify(pub ed25519.PublicKey, message, sig []byte) bool {
	return ed25519.Verify(pub, message, sig)
}

// Challenge generates a random nonce for authentication challenges.
func Challenge() ([]byte, error) {
	nonce := make([]byte, 32)
	_, err := rand.Read(nonce)
	return nonce, err
}
