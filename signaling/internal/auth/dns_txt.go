// Package dns_txt - DNS TXT record-based configuration and command delivery for stealth signaling.
package auth

import (
	"context"
	"fmt"
	"net"
	"strings"
)

// Resolver handles fetching configuration from DNS TXT records.
type Resolver struct {
	Nameserver string
}

// FetchConfig retrieves and parses configuration strings from a domain's TXT records.
func (r *Resolver) FetchConfig(ctx context.Context, domain string) (map[string]string, error) {
	resolver := &net.Resolver{
		PreferGo: true,
		Dial: func(ctx context.Context, network, address string) (net.Conn, error) {
			d := net.Dialer{}
			return d.DialContext(ctx, "udp", r.Nameserver)
		},
	}

	txts, err := resolver.LookupTXT(ctx, domain)
	if err != nil {
		return nil, err
	}

	config := make(map[string]string)
	for _, txt := range txts {
		parts := strings.SplitN(txt, "=", 2)
		if len(parts) == 2 {
			config[strings.TrimSpace(parts[0])] = strings.TrimSpace(parts[1])
		}
	}

	return config, nil
}
