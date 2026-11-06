// EN: tonclient.go - the toncenter v2 HTTP client used by `w5addr activate`: account state, runGetMethod
// EN: wrappers (seqno, get_public_key), sendBoc, with retry on rate limiting.
// RU: tonclient.go - HTTP-клиент toncenter v2 для `w5addr activate`: состояние аккаунта, обёртки
// RU: runGetMethod (seqno, get_public_key), sendBoc, с повторами при ограничении частоты.
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"math/big"
	"net/http"
	"net/url"
	"strings"
	"time"
)

type tgRPC struct {
	base   string
	apiKey string
	http   *http.Client
}

type tgRPCResponse struct {
	OK     bool            `json:"ok"`
	Result json.RawMessage `json:"result"`
	Error  string          `json:"error"`
}

// EN: tgNumber decodes toncenter's balance, which is a string on v2 but may be a bare number.
// RU: tgNumber разбирает balance от toncenter: в v2 это строка, но может быть и голым числом.
type tgNumber string

func (n *tgNumber) UnmarshalJSON(b []byte) error {
	*n = tgNumber(strings.TrimSpace(strings.Trim(string(b), `"`)))
	return nil
}

func (r *tgRPC) call(path string, body any) (json.RawMessage, error) {
	raw, err := json.Marshal(body)
	if err != nil {
		return nil, err
	}
	for attempt := 0; ; attempt++ {
		var req *http.Request
		if body == nil {
			req, err = http.NewRequest(http.MethodGet, r.base+path, nil)
		} else {
			req, err = http.NewRequest(http.MethodPost, r.base+path, bytes.NewReader(raw))
			if err == nil {
				req.Header.Set("Content-Type", "application/json")
			}
		}
		if err != nil {
			return nil, err
		}
		if r.apiKey != "" {
			req.Header.Set("X-API-Key", r.apiKey)
		}
		res, err := r.http.Do(req)
		if err != nil {
			return nil, err
		}
		data, readErr := io.ReadAll(res.Body)
		res.Body.Close()
		if res.StatusCode == http.StatusTooManyRequests && attempt < 5 {
			time.Sleep(time.Duration(1200*(attempt+1)) * time.Millisecond)
			continue
		}
		var out tgRPCResponse
		if readErr != nil || json.Unmarshal(data, &out) != nil || !out.OK {
			if out.Error != "" {
				return nil, fmt.Errorf("toncenter: %s", out.Error)
			}
			return nil, fmt.Errorf("toncenter: HTTP %d (use --api-key or retry later)", res.StatusCode)
		}
		return out.Result, nil
	}
}

type tgState struct {
	State   string   `json:"state"`
	Balance tgNumber `json:"balance"`
	Code    string   `json:"code"`
}

func (r *tgRPC) state(addr string) (*tgState, error) {
	raw, err := r.call("/getAddressInformation?address="+url.QueryEscape(addr), nil)
	if err != nil {
		return nil, err
	}
	var st tgState
	if err := json.Unmarshal(raw, &st); err != nil {
		return nil, fmt.Errorf("toncenter: malformed getAddressInformation: %w", err)
	}
	return &st, nil
}

func (r *tgRPC) getNumber(addr, method string) (*big.Int, error) {
	raw, err := r.call("/runGetMethod", map[string]any{
		"address": addr,
		"method":  method,
		"stack":   []any{},
	})
	if err != nil {
		return nil, err
	}
	var out struct {
		ExitCode int                 `json:"exit_code"`
		Stack    [][]json.RawMessage `json:"stack"`
	}
	if err := json.Unmarshal(raw, &out); err != nil {
		return nil, fmt.Errorf("toncenter: malformed runGetMethod: %w", err)
	}
	if out.ExitCode != 0 {
		return nil, fmt.Errorf("%s: exit code %d", method, out.ExitCode)
	}
	if len(out.Stack) == 0 || len(out.Stack[0]) < 2 {
		return nil, fmt.Errorf("%s: empty result stack", method)
	}
	var v string
	if err := json.Unmarshal(out.Stack[0][1], &v); err != nil {
		return nil, fmt.Errorf("%s: unsupported result value", method)
	}
	v = strings.TrimSpace(strings.Trim(v, `"`))
	n := new(big.Int)
	if strings.HasPrefix(v, "0x") || strings.HasPrefix(v, "-0x") {
		if _, ok := n.SetString(v, 0); !ok {
			return nil, fmt.Errorf("%s: bad number %q", method, v)
		}
	} else if _, ok := n.SetString(v, 10); !ok {
		return nil, fmt.Errorf("%s: bad number %q", method, v)
	}
	return n, nil
}

func (r *tgRPC) sendBoc(boc string) error {
	_, err := r.call("/sendBoc", map[string]any{"boc": boc})
	return err
}
