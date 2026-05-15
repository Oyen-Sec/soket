// Project: Soket.io V7.0
// Module: C2 Component
// Description:  C2 Infrastructure component.


package config

import (
	"fmt"
	"os"
	"strconv"
)

const (
	DefaultListenAddr = "0.0.0.0:443"
	MaxPeers          = 1000
)

type Config struct {
	ListenAddr     string
	TLSCertFile    string
	TLSKeyFile     string
	ServerName     string

	MaxPeers           int
	HandshakeTimeout   int
	SessionTimeout     int
	MaxMessageSize     int
	EnableRateLimiting bool
	RateLimitRPS       int

	EnableP2PRelay     bool
	P2PCheckInterval   int
	P2PMaxRetries      int

	LogLevel           string
	LogFile            string

	EnableCloudflareTunnel bool
	CloudflareTunnelID     string
}

func DefaultConfig() *Config {
	return &Config{
		ListenAddr:           DefaultListenAddr,
		ServerName:           "oyen-sc.serveblog.net",
		MaxPeers:             MaxPeers,
		HandshakeTimeout:     60,
		SessionTimeout:       3600,
		MaxMessageSize:       65536,
		EnableRateLimiting:   true,
		RateLimitRPS:         100,
		EnableP2PRelay:       true,
		P2PCheckInterval:     5,
		P2PMaxRetries:        6,
		LogLevel:             "info",
		EnableCloudflareTunnel: false,
	}
}

func LoadFromEnv() *Config {
	cfg := DefaultConfig()

	if val := os.Getenv("RELAY_LISTEN_ADDR"); val != "" {
		cfg.ListenAddr = val
	}

	if val := os.Getenv("RELAY_TLS_CERT"); val != "" {
		cfg.TLSCertFile = val
	}

	if val := os.Getenv("RELAY_TLS_KEY"); val != "" {
		cfg.TLSKeyFile = val
	}

	if val := os.Getenv("RELAY_SERVER_NAME"); val != "" {
		cfg.ServerName = val
	}

	if val := os.Getenv("RELAY_MAX_PEERS"); val != "" {
		if n, err := strconv.Atoi(val); err == nil {
			cfg.MaxPeers = n
		}
	}

	if val := os.Getenv("RELAY_HANDSHAKE_TIMEOUT"); val != "" {
		if n, err := strconv.Atoi(val); err == nil {
			cfg.HandshakeTimeout = n
		}
	}

	if val := os.Getenv("RELAY_SESSION_TIMEOUT"); val != "" {
		if n, err := strconv.Atoi(val); err == nil {
			cfg.SessionTimeout = n
		}
	}

	if val := os.Getenv("RELAY_LOG_LEVEL"); val != "" {
		cfg.LogLevel = val
	}

	if val := os.Getenv("RELAY_ENABLE_CF_TUNNEL"); val == "true" {
		cfg.EnableCloudflareTunnel = true
	}

	if val := os.Getenv("RELAY_CF_TUNNEL_ID"); val != "" {
		cfg.CloudflareTunnelID = val
	}

	return cfg
}

func (c *Config) Validate() error {
	if c.ListenAddr == "" {
		return fmt.Errorf("listen address is required")
	}

	if c.MaxPeers <= 0 {
		return fmt.Errorf("max peers must be positive")
	}

	if c.HandshakeTimeout <= 0 {
		return fmt.Errorf("handshake timeout must be positive")
	}

	if c.SessionTimeout <= 0 {
		return fmt.Errorf("session timeout must be positive")
	}

	if c.MaxMessageSize <= 0 {
		return fmt.Errorf("max message size must be positive")
	}

	return nil
}

func (c *Config) Is() bool {
	return c.TLSCertFile != "" && c.TLSKeyFile != ""
}
