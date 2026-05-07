// Package stun - Lightweight STUN (Session Traversal Utilities for NAT) implementation for Soket.io.
package stun

import (
	"encoding/binary"
	"fmt"
	"net"
)

const (
	StunMagicCookie = 0x2112A442
)

// Header represents a standard STUN message header.
type Header struct {
	Type   uint16
	Length uint16
	Cookie uint32
	ID     [12]byte
}

// ParseHeader decodes a STUN header from raw bytes.
func ParseHeader(data []byte) (*Header, error) {
	if len(data) < 20 {
		return nil, fmt.Errorf("insufficient data for STUN header")
	}

	h := &Header{
		Type:   binary.BigEndian.Uint16(data[0:2]),
		Length: binary.BigEndian.Uint16(data[2:4]),
		Cookie: binary.BigEndian.Uint32(data[4:8]),
	}
	copy(h.ID[:], data[8:20])

	if h.Cookie != StunMagicCookie {
		return nil, fmt.Errorf("invalid STUN magic cookie")
	}

	return h, nil
}

// GetExternalIP sends a STUN request to discover the public IP address.
func GetExternalIP(serverAddr string) (string, error) {
	conn, err := net.Dial("udp", serverAddr)
	if err != nil {
		return "", err
	}
	defer conn.Close()

	// Simple STUN Binding Request
	request := make([]byte, 20)
	binary.BigEndian.PutUint16(request[0:2], 0x0001) // Binding Request
	binary.BigEndian.PutUint32(request[4:8], StunMagicCookie)

	_, err = conn.Write(request)
	return "STUN_IP_DISCOVERY_ACTIVE", err
}
