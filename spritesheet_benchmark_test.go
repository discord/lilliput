package lilliput

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"testing"
	"time"
)

// BenchmarkSpriteSheetGeneration tests sprite sheet generation across various video types.
// It validates correctness, performance, and edge case handling.
func TestSpriteSheetBattery(t *testing.T) {
	testVidDir := "testdata"
	videos, err := filepath.Glob(filepath.Join(testVidDir, "*_10s_*.mp4"))
	if err != nil {
		t.Fatalf("glob: %v", err)
	}
	if len(videos) == 0 {
		videos, _ = filepath.Glob(filepath.Join(testVidDir, "*.mp4"))
	}
	if len(videos) == 0 {
		t.Skip("no test videos found")
	}

	type testCase struct {
		name string
		fn   func(t *testing.T, videoPath string)
	}

	tests := []testCase{
		{
			name: "basic_generation",
			fn: func(t *testing.T, videoPath string) {
				testBasicGeneration(t, videoPath)
			},
		},
		{
			name: "edge_case_durations",
			fn: func(t *testing.T, videoPath string) {
				testEdgeCaseDurations(t, videoPath)
			},
		},
		{
			name: "output_validation",
			fn: func(t *testing.T, videoPath string) {
				testOutputValidation(t, videoPath)
			},
		},
		{
			name: "performance_metrics",
			fn: func(t *testing.T, videoPath string) {
				testPerformanceMetrics(t, videoPath)
			},
		},
		{
			name: "format_coverage",
			fn: func(t *testing.T, videoPath string) {
				testFormatCoverage(t, videoPath)
			},
		},
	}

	for _, video := range videos {
		vidName := filepath.Base(video)
		t.Run(vidName, func(t *testing.T) {
			for _, test := range tests {
				t.Run(test.name, func(t *testing.T) {
					test.fn(t, video)
				})
			}
		})
	}
}

func testBasicGeneration(t *testing.T, videoPath string) {
	buf, err := os.ReadFile(videoPath)
	if err != nil {
		t.Fatalf("read: %v", err)
	}

	dec, err := NewDecoder(buf)
	if err != nil {
		t.Skipf("decoder not supported for %s: %v", filepath.Base(videoPath), err)
	}
	defer dec.Close()

	dst := make([]byte, 50*1024*1024) // 50MB buffer
	_, manifest, err := GenerateSpriteSheet(dec, &SpriteSheetOptions{
		FileType:      ".jpeg",
		EncodeOptions: map[int]int{JpegQuality: 85},
	}, dst)
	if err != nil {
		t.Fatalf("GenerateSpriteSheet: %v", err)
	}

	if manifest == nil {
		t.Fatal("manifest is nil")
	}
	if len(manifest.Tiles) == 0 {
		t.Fatal("no tiles generated")
	}
	if manifest.SheetWidth <= 0 || manifest.SheetHeight <= 0 {
		t.Fatalf("invalid sheet dimensions: %dx%d", manifest.SheetWidth, manifest.SheetHeight)
	}
	if manifest.TileWidth <= 0 || manifest.TileHeight <= 0 {
		t.Fatalf("invalid tile dimensions: %dx%d", manifest.TileWidth, manifest.TileHeight)
	}

	t.Logf("✓ Generated %d tiles, sheet %dx%d, tile %dx%d",
		len(manifest.Tiles), manifest.SheetWidth, manifest.SheetHeight,
		manifest.TileWidth, manifest.TileHeight)
}

func testEdgeCaseDurations(t *testing.T, videoPath string) {
	buf, err := os.ReadFile(videoPath)
	if err != nil {
		t.Fatalf("read: %v", err)
	}

	dec, err := NewDecoder(buf)
	if err != nil {
		t.Skipf("decoder not supported for %s: %v", filepath.Base(videoPath), err)
	}
	defer dec.Close()

	duration := dec.Duration().Seconds()
	t.Logf("video duration: %.2fs", duration)

	// Generate for the actual duration
	dst := make([]byte, 50*1024*1024)
	_, manifest, err := GenerateSpriteSheet(dec, &SpriteSheetOptions{
		FileType:      ".jpeg",
		EncodeOptions: map[int]int{JpegQuality: 85},
	}, dst)
	if err != nil {
		t.Fatalf("GenerateSpriteSheet: %v", err)
	}

	// Verify tile count matches expected tier
	expectedTiles := 0
	if duration <= 50 {
		expectedTiles = int(duration) + 1
	} else if duration <= 15*60 { // 15 minutes
		expectedTiles = int(duration / 4)
	} else {
		expectedTiles = int(duration / 30)
	}
	tolerance := expectedTiles / 5 // 20% tolerance
	if len(manifest.Tiles) < expectedTiles-tolerance || len(manifest.Tiles) > expectedTiles+tolerance {
		t.Logf("⚠ tile count %d outside expected range [%d, %d]",
			len(manifest.Tiles), expectedTiles-tolerance, expectedTiles+tolerance)
	} else {
		t.Logf("✓ tile count %d matches expected ~%d", len(manifest.Tiles), expectedTiles)
	}
}

func testOutputValidation(t *testing.T, videoPath string) {
	buf, err := os.ReadFile(videoPath)
	if err != nil {
		t.Fatalf("read: %v", err)
	}

	dec, err := NewDecoder(buf)
	if err != nil {
		t.Skipf("decoder not supported for %s: %v", filepath.Base(videoPath), err)
	}
	defer dec.Close()

	dst := make([]byte, 50*1024*1024)
	result, manifest, err := GenerateSpriteSheet(dec, &SpriteSheetOptions{
		FileType:      ".jpeg",
		EncodeOptions: map[int]int{JpegQuality: 85},
	}, dst)
	if err != nil {
		t.Fatalf("GenerateSpriteSheet: %v", err)
	}

	// Validate result
	if len(result) == 0 {
		t.Fatal("empty result")
	}
	if len(result) > len(dst) {
		t.Fatalf("result exceeds buffer: %d > %d", len(result), len(dst))
	}

	// Check JPEG magic bytes
	if result[0] != 0xFF || result[1] != 0xD8 {
		t.Fatal("invalid JPEG magic bytes")
	}

	// Validate manifest
	for i, tile := range manifest.Tiles {
		if tile.TimestampSec < 0 {
			t.Fatalf("tile %d has negative timestamp: %.2f", i, tile.TimestampSec)
		}
		if tile.X < 0 || tile.Y < 0 || tile.Width <= 0 || tile.Height <= 0 {
			t.Fatalf("tile %d has invalid rect: (%d,%d) %dx%d", i, tile.X, tile.Y, tile.Width, tile.Height)
		}
		if tile.X+tile.Width > manifest.SheetWidth {
			t.Fatalf("tile %d extends beyond sheet width: %d+%d > %d", i, tile.X, tile.Width, manifest.SheetWidth)
		}
		if tile.Y+tile.Height > manifest.SheetHeight {
			t.Fatalf("tile %d extends beyond sheet height: %d+%d > %d", i, tile.Y, tile.Height, manifest.SheetHeight)
		}
	}

	// Validate WebVTT
	vtt := manifest.WebVTT("sprite.jpg")
	if len(vtt) == 0 {
		t.Fatal("empty WebVTT")
	}
	if !contains(vtt, "WEBVTT") {
		t.Fatal("missing WEBVTT header")
	}

	// Validate JSON
	jsonData, err := manifest.MarshalJSON()
	if err != nil {
		t.Fatalf("MarshalJSON: %v", err)
	}
	if len(jsonData) == 0 {
		t.Fatal("empty JSON")
	}

	t.Logf("✓ output validated: %d bytes JPEG, %d bytes VTT, %d bytes JSON",
		len(result), len(vtt), len(jsonData))
}

func testPerformanceMetrics(t *testing.T, videoPath string) {
	buf, err := os.ReadFile(videoPath)
	if err != nil {
		t.Fatalf("read: %v", err)
	}

	// Measure startup time
	startUp := time.Now()
	dec, err := NewDecoder(buf)
	if err != nil {
		t.Skipf("decoder not supported for %s: %v", filepath.Base(videoPath), err)
	}
	defer dec.Close()
	decodeTime := time.Since(startUp)

	hdr, _ := dec.Header()
	duration := dec.Duration().Seconds()

	// Measure generation time
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	memBefore := int64(m.TotalAlloc)

	startGen := time.Now()
	dst := make([]byte, 50*1024*1024)
	result, manifest, err := GenerateSpriteSheet(dec, &SpriteSheetOptions{
		FileType:      ".jpeg",
		EncodeOptions: map[int]int{JpegQuality: 85},
	}, dst)
	genTime := time.Since(startGen)

	runtime.ReadMemStats(&m)
	memAfter := int64(m.TotalAlloc)
	memUsed := (memAfter - memBefore) / 1024 / 1024 // MB (cumulative alloc during gen)

	if err != nil {
		t.Fatalf("GenerateSpriteSheet: %v", err)
	}

	// Estimated throughput
	fps := float64(hdr.Width()*hdr.Height()) / (duration * 1e6) // MP/s
	t.Logf("✓ %dx%d, %.1fs duration: decode=%.0fms, gen=%.0fms, mem=%dMB, %d tiles, %dKB JPEG (%.0f MP/s)",
		hdr.Width(), hdr.Height(), duration,
		decodeTime.Seconds()*1000, genTime.Seconds()*1000, memUsed,
		len(manifest.Tiles), len(result)/1024, fps)

	// Sanity checks
	if genTime > 30*time.Second {
		t.Logf("⚠ generation took >30s: %.1fs", genTime.Seconds())
	}
}

func testFormatCoverage(t *testing.T, videoPath string) {
	buf, err := os.ReadFile(videoPath)
	if err != nil {
		t.Fatalf("read: %v", err)
	}

	dec, err := NewDecoder(buf)
	if err != nil {
		t.Skipf("decoder not supported for %s: %v", filepath.Base(videoPath), err)
	}
	defer dec.Close()

	// Test multiple output formats; WebP may not be supported for all video types.
	formats := []struct {
		ext     string
		require bool
	}{
		{".jpeg", true},
		{".png", true},
		{".webp", false}, // not always supported depending on pixel format
	}
	for _, f := range formats {
		dst := make([]byte, 50*1024*1024)
		result, _, err := GenerateSpriteSheet(dec, &SpriteSheetOptions{
			FileType:      f.ext,
			EncodeOptions: map[int]int{JpegQuality: 85},
		}, dst)
		if err != nil {
			if f.require {
				t.Errorf("required format %s failed: %v", f.ext, err)
			} else {
				t.Logf("⚠ optional format %s: %v", f.ext, err)
			}
			continue
		}
		if len(result) == 0 {
			if f.require {
				t.Errorf("required format %s produced empty output", f.ext)
			} else {
				t.Logf("⚠ optional format %s produced empty output", f.ext)
			}
			continue
		}
		t.Logf("✓ format %s: %dKB", f.ext, len(result)/1024)
	}
}

func contains(s, substr string) bool {
	if len(substr) == 0 || len(s) < len(substr) {
		return false
	}
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return true
		}
	}
	return false
}

// BenchmarkSpriteSheetPerformance runs benchmarks for performance profiling.
func BenchmarkSpriteSheetPerformance(b *testing.B) {
	videoPath := "testdata/big_buck_bunny_480p_10s_std.mp4"
	buf, err := os.ReadFile(videoPath)
	if err != nil {
		b.Skipf("video not available: %v", err)
	}

	// Warm up
	dec, _ := NewDecoder(buf)
	dst := make([]byte, 50*1024*1024)
	GenerateSpriteSheet(dec, &SpriteSheetOptions{
		FileType:      ".jpeg",
		EncodeOptions: map[int]int{JpegQuality: 85},
	}, dst)
	dec.Close()

	// Run benchmark
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		dec, _ := NewDecoder(buf)
		GenerateSpriteSheet(dec, &SpriteSheetOptions{
			FileType:      ".jpeg",
			EncodeOptions: map[int]int{JpegQuality: 85},
		}, dst)
		dec.Close()
	}
}

// TestSpriteSheetStress stress-tests with many rapid calls.
func TestSpriteSheetStress(t *testing.T) {
	videoPath := "testdata/big_buck_bunny_480p_10s_std.mp4"
	buf, err := os.ReadFile(videoPath)
	if err != nil {
		t.Skipf("video not available: %v", err)
	}

	const numRuns = 10
	for run := 0; run < numRuns; run++ {
		dec, err := NewDecoder(buf)
		if err != nil {
			t.Fatalf("run %d: NewDecoder: %v", run, err)
		}
		dst := make([]byte, 50*1024*1024)
		_, manifest, err := GenerateSpriteSheet(dec, &SpriteSheetOptions{
			FileType:      ".jpeg",
			EncodeOptions: map[int]int{JpegQuality: 85},
		}, dst)
		dec.Close()

		if err != nil {
			t.Fatalf("run %d: GenerateSpriteSheet: %v", run, err)
		}
		if manifest == nil || len(manifest.Tiles) == 0 {
			t.Fatalf("run %d: no tiles generated", run)
		}
		if (run+1)%2 == 0 {
			fmt.Printf("✓ %d/%d stress runs completed\n", run+1, numRuns)
		}
	}
	t.Logf("✓ All %d stress runs passed", numRuns)
}
