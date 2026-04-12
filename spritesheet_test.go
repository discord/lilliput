package lilliput

import (
	"os"
	"strings"
	"testing"
	"time"
)

// TestSelectTimestamps verifies the tile count and distribution rules from the RFC.
func TestSelectTimestamps(t *testing.T) {
	tests := []struct {
		name         string
		duration     time.Duration
		wantMinTiles int
		wantMaxTiles int
	}{
		{"zero duration", 0, 0, 0},
		{"1 second", 1 * time.Second, 1, 1},
		{"10 seconds", 10 * time.Second, 10, 10},
		{"50 seconds boundary", 50 * time.Second, 50, 50},
		{"51 seconds → medium", 51 * time.Second, 50, 50},
		{"5 minutes → medium", 5 * time.Minute, 50, 50},
		{"15 minutes boundary → medium", 15 * time.Minute, 50, 50},
		{"16 minutes → long", 16 * time.Minute, 100, 100},
		{"2 hours → long", 2 * time.Hour, 100, 100},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := selectTimestamps(tt.duration, nil, 0)
			if len(got) < tt.wantMinTiles || len(got) > tt.wantMaxTiles {
				t.Errorf("selectTimestamps(%v) = %d tiles, want [%d, %d]",
					tt.duration, len(got), tt.wantMinTiles, tt.wantMaxTiles)
			}
			// Timestamps must be monotonically non-decreasing.
			for i := 1; i < len(got); i++ {
				if got[i] < got[i-1] {
					t.Errorf("timestamps not monotonic at index %d: %v < %v", i, got[i], got[i-1])
				}
			}
			// All timestamps must be within [0, duration).
			durationSec := tt.duration.Seconds()
			for i, ts := range got {
				if ts < 0 || ts >= durationSec {
					t.Errorf("timestamp[%d]=%v out of range [0, %v)", i, ts, durationSec)
				}
			}
		})
	}
}

// TestTileDimensions verifies aspect-ratio-preserving tile sizing.
func TestTileDimensions(t *testing.T) {
	tests := []struct {
		name       string
		srcW, srcH int
		maxDim     int
		wantW      int
		wantH      int
	}{
		{"landscape 1920x1080", 1920, 1080, 160, 160, 90},
		{"portrait 1080x1920", 1080, 1920, 160, 90, 160},
		{"square 500x500", 500, 500, 160, 160, 160},
		{"tiny 10x10", 10, 10, 160, 160, 160},
		{"wide 2560x1080", 2560, 1080, 160, 160, 67},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotW, gotH := tileDimensions(tt.srcW, tt.srcH, tt.maxDim)
			if gotW != tt.wantW || gotH != tt.wantH {
				t.Errorf("tileDimensions(%d,%d,%d) = (%d,%d), want (%d,%d)",
					tt.srcW, tt.srcH, tt.maxDim, gotW, gotH, tt.wantW, tt.wantH)
			}
		})
	}
}

// TestSpriteSheetManifestWebVTT verifies WebVTT output format.
func TestSpriteSheetManifestWebVTT(t *testing.T) {
	m := &SpriteSheetManifest{
		SheetWidth:  800,
		SheetHeight: 90,
		TileWidth:   160,
		TileHeight:  90,
		GridColumns: 5,
		Tiles: []SpriteSheetTile{
			{TimestampSec: 0, X: 0, Y: 0, Width: 160, Height: 90},
			{TimestampSec: 5, X: 160, Y: 0, Width: 160, Height: 90},
			{TimestampSec: 10, X: 320, Y: 0, Width: 160, Height: 90},
		},
	}
	vtt := m.WebVTT("https://cdn.example.com/sheet.jpg")
	if !strings.HasPrefix(vtt, "WEBVTT\n") {
		t.Errorf("WebVTT output missing WEBVTT header: %q", vtt[:20])
	}
	if !strings.Contains(vtt, "#xywh=0,0,160,90") {
		t.Errorf("WebVTT missing first tile xywh: %s", vtt)
	}
	if !strings.Contains(vtt, "#xywh=160,0,160,90") {
		t.Errorf("WebVTT missing second tile xywh: %s", vtt)
	}
	// Verify timing format.
	if !strings.Contains(vtt, "00:00:00.000 --> 00:00:05.000") {
		t.Errorf("WebVTT first cue timing incorrect: %s", vtt)
	}
}

// TestSpriteSheetManifestJSON verifies JSON serialization.
func TestSpriteSheetManifestJSON(t *testing.T) {
	m := &SpriteSheetManifest{
		SheetWidth:  320,
		SheetHeight: 90,
		TileWidth:   160,
		TileHeight:  90,
		GridColumns: 5,
		Tiles: []SpriteSheetTile{
			{TimestampSec: 0, X: 0, Y: 0, Width: 160, Height: 90},
		},
	}
	data, err := m.MarshalJSON()
	if err != nil {
		t.Fatalf("MarshalJSON error: %v", err)
	}
	s := string(data)
	if !strings.Contains(s, `"sheet_width":320`) {
		t.Errorf("JSON missing sheet_width: %s", s)
	}
	if !strings.Contains(s, `"timestamp_sec":0`) {
		t.Errorf("JSON missing timestamp_sec: %s", s)
	}
}

// TestGenerateSpriteSheet_NonVideoDecoder verifies that a non-video decoder returns an error.
func TestGenerateSpriteSheet_NonVideoDecoder(t *testing.T) {
	// Use a PNG image (openCVDecoder), which is not an avCodecDecoder.
	pngPath := "testdata/tears_of_steel_fragment.jpg"
	if _, err := os.Stat(pngPath); os.IsNotExist(err) {
		t.Skip("test image not available; skipping non-video decoder test")
	}
	buf, err := os.ReadFile(pngPath)
	if err != nil {
		t.Skip("could not read test image")
	}
	dec, err := NewDecoder(buf)
	if err != nil {
		t.Skip("could not create decoder for test image")
	}
	defer dec.Close()

	_, _, err = GenerateSpriteSheet(dec, nil, make([]byte, 8*1024*1024))
	if err == nil {
		t.Error("expected error for non-video decoder, got nil")
	}
}

// TestGenerateSpriteSheet_Video is an integration test against real video files.
func TestGenerateSpriteSheet_Video(t *testing.T) {
	// Try the dedicated test video first, then fall back to existing testdata.
	videoPath := "testdata/spritesheet_test.mp4"
	if _, err := os.Stat(videoPath); os.IsNotExist(err) {
		videoPath = "testdata/big_buck_bunny_480p_10s_std.mp4"
	}
	if _, err := os.Stat(videoPath); os.IsNotExist(err) {
		t.Skip("no test video present; skipping integration test")
	}

	buf, err := os.ReadFile(videoPath)
	if err != nil {
		t.Fatalf("could not read test video: %v", err)
	}

	dec, err := NewDecoder(buf)
	if err != nil {
		t.Fatalf("NewDecoder: %v", err)
	}
	defer dec.Close()

	dst := make([]byte, 8*1024*1024)
	result, manifest, err := GenerateSpriteSheet(dec, &SpriteSheetOptions{
		FileType:      ".jpeg",
		EncodeOptions: map[int]int{JpegQuality: 80},
	}, dst)
	if err != nil {
		t.Fatalf("GenerateSpriteSheet: %v", err)
	}
	if len(result) == 0 {
		t.Error("GenerateSpriteSheet returned empty result")
	}
	if manifest == nil {
		t.Fatal("GenerateSpriteSheet returned nil manifest")
	}
	if len(manifest.Tiles) == 0 {
		t.Error("manifest has no tiles")
	}
	if manifest.SheetWidth <= 0 || manifest.SheetHeight <= 0 {
		t.Errorf("manifest has invalid dimensions: %dx%d", manifest.SheetWidth, manifest.SheetHeight)
	}
	if manifest.GridColumns != SpriteSheetGridColumns {
		t.Errorf("manifest GridColumns = %d, want %d", manifest.GridColumns, SpriteSheetGridColumns)
	}

	// Verify WebVTT generation doesn't panic.
	vtt := manifest.WebVTT("https://cdn.discordapp.com/sheet.jpg")
	if !strings.HasPrefix(vtt, "WEBVTT") {
		t.Errorf("WebVTT output malformed: %q", vtt[:minInt(len(vtt), 40)])
	}

	t.Logf("sprite sheet: %d bytes, %d tiles, %dx%d sheet",
		len(result), len(manifest.Tiles), manifest.SheetWidth, manifest.SheetHeight)
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}
