// Package ratelimit - Advanced token bucket rate limiting for Soket.io infrastructure.
package ratelimit

import (
	"sync"
	"time"
)

// Limiter implements a thread-safe token bucket rate limiter.
type Limiter struct {
	rate       float64
	capacity   float64
	tokens     float64
	lastUpdate time.Time
	mu         sync.Mutex
}

// NewLimiter creates a new token bucket limiter with specified rate (tokens/sec) and capacity.
func NewLimiter(rate, capacity float64) *Limiter {
	return &Limiter{
		rate:       rate,
		capacity:   capacity,
		tokens:     capacity,
		lastUpdate: time.Now(),
	}
}

// Allow returns true if a token is available, otherwise false.
func (l *Limiter) Allow() bool {
	l.mu.Lock()
	defer l.mu.Unlock()

	now := time.Now()
	elapsed := now.Sub(l.lastUpdate).Seconds()
	l.tokens += elapsed * l.rate

	if l.tokens > l.capacity {
		l.tokens = l.capacity
	}

	l.lastUpdate = now

	if l.tokens >= 1 {
		l.tokens--
		return true
	}

	return false
}

// SetRate dynamically updates the token generation rate.
func (l *Limiter) SetRate(newRate float64) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.rate = newRate
}
