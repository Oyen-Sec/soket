// Package dga - Domain Generation Algorithm (DGA) for resilient C2 infrastructure discovery.
package dga

import (
	"crypto/sha256"
	"fmt"
	"time"
)

// Generator uces time-based domain names for infrastructure resilience.
type Generator struct {
	Seed   string
	Suffix string
}

// NewGenerator creates a DGA generator with a specific seed and TLD.
func NewGenerator(seed, suffix string) *Generator {
	return &Generator{Seed: seed, Suffix: suffix}
}

// GenerateForDate creates a domain name based on the provided time.
func (g *Generator) GenerateForDate(t time.Time) string {
	dateStr := t.Format("2006-01-02")
	hash := sha256.Sum256([]byte(g.Seed + dateStr))
	
	// Use first 12 bytes of hash for domain name
	domain := fmt.Sprintf("%x", hash[:6])
	return domain + "." + g.Suffix
}

// GetActiveDomains returns current, previous, and next domains for overlap support.
func (g *Generator) GetActiveDomains() []string {
	now := time.Now()
	return []string{
		g.GenerateForDate(now.AddDate(0, 0, -1)),
		g.GenerateForDate(now),
		g.GenerateForDate(now.AddDate(0, 0, 1)),
	}
}
