package api

import (
	"crypto/hmac"
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"
)

func hashPassword(password string) (string, error) {
	raw := make([]byte, 16)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	salt := hex.EncodeToString(raw)
	digest, err := pbkdf2.Key(sha256.New, password, []byte(salt), 100_000, 32)
	if err != nil {
		return "", err
	}
	return "pbkdf2$" + salt + "$" + hex.EncodeToString(digest), nil
}

func verifyPassword(password, stored string) bool {
	parts := strings.Split(stored, "$")
	if len(parts) != 3 || parts[0] != "pbkdf2" {
		return false
	}
	digest, err := pbkdf2.Key(sha256.New, password, []byte(parts[1]), 100_000, 32)
	if err != nil {
		return false
	}
	return hmac.Equal([]byte(hex.EncodeToString(digest)), []byte(parts[2]))
}

func sha256Hex(value string) string {
	sum := sha256.Sum256([]byte(value))
	return hex.EncodeToString(sum[:])
}

func b64url(raw []byte) string {
	return base64.RawURLEncoding.EncodeToString(raw)
}

type jwtClaims struct {
	Subject string `json:"sub"`
	Issued  int64  `json:"iat"`
	Expires int64  `json:"exp"`
	ID      string `json:"jti"`
}

func issueJWT(secret string, userID int64, jti string, now time.Time) (string, time.Time, error) {
	exp := now.Add(7 * 24 * time.Hour)
	header := b64url([]byte(`{"alg":"HS256","typ":"JWT"}`))
	body, err := json.Marshal(jwtClaims{
		Subject: strconv.FormatInt(userID, 10),
		Issued:  now.Unix(),
		Expires: exp.Unix(),
		ID:      jti,
	})
	if err != nil {
		return "", time.Time{}, err
	}
	payload := header + "." + b64url(body)
	mac := hmac.New(sha256.New, []byte(secret))
	_, _ = mac.Write([]byte(payload))
	return payload + "." + b64url(mac.Sum(nil)), exp, nil
}

func parseJWT(secret, token string, now time.Time) (jwtClaims, error) {
	parts := strings.Split(token, ".")
	if len(parts) != 3 {
		return jwtClaims{}, errors.New("登录已失效")
	}
	mac := hmac.New(sha256.New, []byte(secret))
	_, _ = mac.Write([]byte(parts[0] + "." + parts[1]))
	sig, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil || !hmac.Equal(sig, mac.Sum(nil)) {
		return jwtClaims{}, errors.New("登录已失效")
	}
	raw, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return jwtClaims{}, errors.New("登录已失效")
	}
	var claims jwtClaims
	if err := json.Unmarshal(raw, &claims); err != nil || claims.Subject == "" || claims.ID == "" {
		return jwtClaims{}, errors.New("登录已失效")
	}
	if claims.Expires <= now.Unix() {
		return jwtClaims{}, errors.New("登录已失效")
	}
	return claims, nil
}

func newID() (string, error) {
	raw := make([]byte, 16)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	return b64url(raw), nil
}

func issueTicket(secret, room string, userID int64, now time.Time) (string, time.Time) {
	exp := now.Add(12 * time.Hour)
	payload := fmt.Sprintf("%s\n%d\n%d", room, userID, exp.Unix())
	mac := hmac.New(sha256.New, []byte(secret))
	_, _ = mac.Write([]byte(payload))
	return b64url([]byte(payload)) + "." + b64url(mac.Sum(nil)), exp
}
