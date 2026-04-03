package lilliput

import (
	"testing"
	"time"
)

func TestSpriteSheetIntervalRules(t *testing.T) {
	// Custom rules for testing
	rules := []SpriteSheetIntervalRule{
		{MaxDuration: 10 * time.Second, Interval: 2 * time.Second, MaxTiles: 20},
		{MaxDuration: 30 * time.Second, Interval: 0, MaxTiles: 12}, // exactly 12 tiles
		{MaxDuration: 0, Interval: 5 * time.Second, MaxTiles: 100}, // fallback
	}

	tests := []struct {
		name        string
		duration    time.Duration
		absMaxTiles int
		wantTiles   int
	}{
		{"under 10s uses 2s interval", 8 * time.Second, 100, 4}, // 8 / 2 = 4
		{"10s uses 2s interval", 10 * time.Second, 100, 5},      // 10 / 2 = 5
		{"under 30s uses exactly 12 tiles", 20 * time.Second, 100, 12},
		{"over 30s uses 5s interval", 60 * time.Second, 100, 12},           // 60 / 5 = 12
		{"over 30s capped by max tiles", 1000 * time.Second, 100, 100},     // 1000 / 5 = 200, capped at 100
		{"safety cap limits large count", 1000 * time.Second, 50, 50},      // 200 capped at 100 by rule, then 50 by abs
		{"empty rules falls back to RFC dense", 40 * time.Second, 100, 40}, // 40s / 1s = 40 (only when rules=nil, tested below)
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var r []SpriteSheetIntervalRule
			if tt.name != "empty rules falls back to RFC dense" {
				r = rules
			}

			got := selectTimestamps(tt.duration, r, tt.absMaxTiles)
			if len(got) != tt.wantTiles {
				t.Errorf("selectTimestamps(%v) = %d tiles, want %d", tt.duration, len(got), tt.wantTiles)
			}
		})
	}
}
