// Project: Soket.io V7.0
// Module: C2 Component
// Description: Professional C2 Infrastructure component.

package signaling

import (
	"context"
	"fmt"
	"sync"
)

// Message represents a signaling message between peers.
type Message struct {
	Type    string `json:"type"`
	Payload string `json:"payload"`
	From    string `json:"from"`
	To      string `json:"to"`
}

// Client manages signaling for a specific peer session.
type Client struct {
	ID         string
	Inbox      chan *Message
	ctx        context.Context
	cancel     context.CancelFunc
	mu         sync.RWMutex
}

// NewClient initializes a signaling client for the given peer ID.
func NewClient(ctx context.Context, id string) *Client {
	cCtx, cancel := context.WithCancel(ctx)
	return &Client{
		ID:    id,
		Inbox: make(chan *Message, 100),
		ctx:   cCtx,
		cancel: cancel,
	}
}

// Send queues a message to the client's inbox.
func (c *Client) Send(msg *Message) error {
	select {
	case c.Inbox <- msg:
		return nil
	case <-c.ctx.Done():
		return fmt.Errorf("client %s signaling closed", c.ID)
	default:
		return fmt.Errorf("client %s inbox full", c.ID)
	}
}

// Receive blocks until a message is received or context is cancelled.
func (c *Client) Receive() (*Message, error) {
	select {
	case msg := <-c.Inbox:
		return msg, nil
	case <-c.ctx.Done():
		return nil, c.ctx.Err()
	}
}

// Close shuts down the signaling client.
func (c *Client) Close() {
	c.cancel()
}
