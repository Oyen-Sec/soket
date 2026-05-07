// Package relay - Core relay logic for high-performance traffic redirection in Soket.io.
package relay

import (
	"context"
	"io"
	"net"
	"sync"
)

// Proxy handles the bidirectional data transfer between two network connections.
type Proxy struct {
	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup
}

// NewProxy initializes a new Proxy instance with a context.
func NewProxy(parent context.Context) *Proxy {
	ctx, cancel := context.WithCancel(parent)
	return &Proxy{
		ctx:    ctx,
		cancel: cancel,
	}
}

// Tunnel bridges two net.Conn objects and blocks until completion or error.
func (p *Proxy) Tunnel(src, dst net.Conn) error {
	p.wg.Add(2)
	errChan := make(chan error, 2)

	go func() {
		defer p.wg.Done()
		_, err := io.Copy(dst, src)
		errChan <- err
	}()

	go func() {
		defer p.wg.Done()
		_, err := io.Copy(src, dst)
		errChan <- err
	}()

	// Wait for first error or completion
	err := <-errChan
	p.cancel()
	src.Close()
	dst.Close()
	p.wg.Wait()
	return err
}
