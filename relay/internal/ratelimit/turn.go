// Package turn - TURN (Traversal Using Relays around NAT) protocol implementation for fallback connectivity.
package turn

import (
	"fmt"
	"net"
	"sync"
)

// Allocation represents a reserved relay resource on the TURN server.
type Allocation struct {
	RelayAddr  *net.UDPAddr
	ClientAddr *net.UDPAddr
	Expiry     int64
	mu         sync.RWMutex
}

// Manager handles the lifecycle of TURN allocations.
type Manager struct {
	allocations map[string]*Allocation
	mu          sync.Mutex
}

// NewManager initializes a TURN allocation manager.
func NewManager() *Manager {
	return &Manager{
		allocations: make(map[string]*Allocation),
	}
}

// CreateAllocation reserves a new relay port for a client.
func (m *Manager) CreateAllocation(clientAddr *net.UDPAddr) (*Allocation, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	id := clientAddr.String()
	if _, exists := m.allocations[id]; exists {
		return nil, fmt.Errorf("allocation already exists for %s", id)
	}

	alloc := &Allocation{
		ClientAddr: clientAddr,
	}
	m.allocations[id] = alloc
	return alloc, nil
}

// DeleteAllocation removes an expired or closed allocation.
func (m *Manager) DeleteAllocation(id string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	delete(m.allocations, id)
}
