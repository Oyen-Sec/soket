// Package tor - Tor Onion Services integration for anonymous signaling and relay communication.
package auth

import (
	"context"
	"fmt"
	"net"

	"golang.org/x/net/proxy"
)

// TorProxy facilitates connections through a local Tor SOCKS proxy.
type TorProxy struct {
	ProxyAddr string
}

// NewTorProxy creates a proxy handler for Tor network access.
func NewTorProxy(addr string) *TorProxy {
	return &TorProxy{ProxyAddr: addr}
}

// Dial establishes a connection through the Tor network.
func (t *TorProxy) Dial(network, addr string) (net.Conn, error) {
	dialer, err := proxy.SOCKS5("tcp", t.ProxyAddr, nil, proxy.Direct)
	if err != nil {
		return nil, fmt.Errorf("tor proxy connection failed: %w", err)
	}

	contextDialer, ok := dialer.(proxy.ContextDialer)
	if !ok {
		return dialer.Dial(network, addr)
	}

	return contextDialer.DialContext(context.Background(), network, addr)
}
