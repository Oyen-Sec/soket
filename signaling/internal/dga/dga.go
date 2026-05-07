package dga

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"time"
)

// Generator implements a time-seeded Domain Generation Algorithm.
type Generator struct {
	Seed string
}

// NewGenerator creates a DGA instance with a static seed.
func NewGenerator(seed string) *Generator {
	return &Generator{Seed: seed}
}

// GenerateForDate creates a list of deterministic domains for a given time.
func (g *Generator) GenerateForDate(t time.Time, count int) []string {
	domains := make([]string, count)
	base := t.Format("2006-01-02")
	
	for i := 0; i < count; i++ {
		input := fmt.Sprintf("%s-%s-%d", base, g.Seed, i)
		hash := sha256.Sum256([]byte(input))
		hashStr := hex.EncodeToString(hash[:])
		domains[i] = fmt.Sprintf("%s.soket-infra.io", hashStr[:12])
	}
	
	return domains
}

// GetCurrentDomains returns the active domains for today.
func (g *Generator) GetCurrentDomains() []string {
	return g.GenerateForDate(time.Now(), 5)
}
