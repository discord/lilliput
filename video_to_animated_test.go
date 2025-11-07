package lilliput

import (
	"os"
	"testing"
	"time"
)

func TestVideoToAnimatedWebP(t *testing.T) {
	t.Run("BasicVideoToAnimatedWebP", testBasicVideoToAnimatedWebP)
	t.Run("VideoToAnimatedWebPWithCustomSampleInterval", testVideoToAnimatedWebPWithCustomSampleInterval)
	t.Run("VideoToAnimatedWebPWithMaxFrames", testVideoToAnimatedWebPWithMaxFrames)
	t.Run("VideoToAnimatedWebPWithZeroInterval", testVideoToAnimatedWebPWithZeroInterval)
	t.Run("VideoToAnimatedWebPVerifyFrameCount", testVideoToAnimatedWebPVerifyFrameCount)
	t.Run("VideoToAnimatedWebPWithResizing", testVideoToAnimatedWebPWithResizing)
	t.Run("VideoToAnimatedWebPWithNonAlignedDimensions", testVideoToAnimatedWebPWithNonAlignedDimensions)
}

func testBasicVideoToAnimatedWebP(t *testing.T) {
	// Load a test video file
	videoData, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("Failed to read test video: %v", err)
	}

	// Create decoder
	decoder, err := newAVCodecDecoder(videoData)
	if err != nil {
		t.Fatalf("Failed to create decoder: %v", err)
	}
	defer decoder.Close()

	// Prepare transformation options
	dstBuf := make([]byte, 50*1024*1024) // 50MB buffer for output
	options := &ImageOptions{
		FileType:                   ".webp",
		Width:                      200,
		Height:                     200,
		ResizeMethod:               ImageOpsFit,
		EncodeOptions:              map[int]int{WebpQuality: 75},
		VideoFrameSampleIntervalMs: 1000,
		EncodeTimeout:              time.Second * 30,
	}

	// Transform video to animated WebP
	ops := NewImageOps(2000)
	defer ops.Close()

	output, err := ops.Transform(decoder, options, dstBuf)
	if err != nil {
		t.Fatalf("Transform failed: %v", err)
	}

	if len(output) == 0 {
		t.Fatal("Transform returned empty output")
	}

	// Verify output is a valid WebP
	if len(output) < 12 {
		t.Fatal("Output too small to be a valid WebP")
	}

	// Check RIFF header
	if string(output[0:4]) != "RIFF" {
		t.Errorf("Expected RIFF header, got %s", string(output[0:4]))
	}

	// Check WEBP signature
	if string(output[8:12]) != "WEBP" {
		t.Errorf("Expected WEBP signature, got %s", string(output[8:12]))
	}

	// Write output for manual inspection if desired
	if err := os.WriteFile("testdata/out/video_to_animated_basic.webp", output, 0644); err != nil {
		t.Logf("Warning: Failed to write output file: %v", err)
	}

	t.Logf("Successfully converted video to animated WebP, output size: %d bytes", len(output))
}

func testVideoToAnimatedWebPWithCustomSampleInterval(t *testing.T) {
	videoData, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("Failed to read test video: %v", err)
	}

	testCases := []struct {
		name     string
		interval int
		maxSize  int
	}{
		{"500ms intervals", 500, 20},
		{"2 second intervals", 2000, 5},
		{"250ms intervals", 250, 40},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, err := newAVCodecDecoder(videoData)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, 50*1024*1024)
			options := &ImageOptions{
				FileType:                   ".webp",
				Width:                      200,
				Height:                     200,
				ResizeMethod:               ImageOpsFit,
				EncodeOptions:              map[int]int{WebpQuality: 75},
				VideoFrameSampleIntervalMs: tc.interval,
				EncodeTimeout:              time.Second * 30,
			}

			ops := NewImageOps(2000)
			defer ops.Close()

			output, err := ops.Transform(decoder, options, dstBuf)
			if err != nil {
				t.Fatalf("Transform failed: %v", err)
			}

			if len(output) == 0 {
				t.Fatal("Transform returned empty output")
			}

			t.Logf("Converted video with %s sample interval, output size: %d bytes", tc.name, len(output))
		})
	}
}

func testVideoToAnimatedWebPWithMaxFrames(t *testing.T) {
	videoData, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("Failed to read test video: %v", err)
	}

	testCases := []struct {
		name               string
		maxFrames          int
		sampleInterval     int
		expectedFrameCount int
	}{
		{"MaxFrames 5", 5, 1000, 5},
		{"MaxFrames 3", 3, 500, 3},
		{"MaxFrames 10", 10, 1000, 10},
		{"MaxFrames 1 (single frame)", 1, 1000, 1},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, err := newAVCodecDecoder(videoData)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, 50*1024*1024)
			options := &ImageOptions{
				FileType:                   ".webp",
				Width:                      200,
				Height:                     200,
				ResizeMethod:               ImageOpsFit,
				EncodeOptions:              map[int]int{WebpQuality: 75},
				VideoFrameSampleIntervalMs: tc.sampleInterval,
				MaxEncodeFrames:            tc.maxFrames,
				EncodeTimeout:              time.Second * 30,
			}

			ops := NewImageOps(2000)
			defer ops.Close()

			output, err := ops.Transform(decoder, options, dstBuf)
			if err != nil {
				t.Fatalf("Transform failed: %v", err)
			}

			if len(output) == 0 {
				t.Fatal("Transform returned empty output")
			}

			// Decode the output to verify frame count
			outputDecoder, err := newWebpDecoder(output)
			if err != nil {
				t.Fatalf("Failed to decode output WebP: %v", err)
			}
			defer outputDecoder.Close()

			header, err := outputDecoder.Header()
			if err != nil {
				t.Fatalf("Failed to get header: %v", err)
			}

			actualFrameCount := header.NumFrames()
			if actualFrameCount != tc.expectedFrameCount {
				t.Errorf("Expected %d frames, got %d frames", tc.expectedFrameCount, actualFrameCount)
			}

			t.Logf("Successfully limited to %d frames (expected %d), output size: %d bytes",
				actualFrameCount, tc.expectedFrameCount, len(output))
		})
	}
}

func testVideoToAnimatedWebPWithZeroInterval(t *testing.T) {
	// With interval = 0, should only extract first frame (default behavior)
	videoData, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("Failed to read test video: %v", err)
	}

	decoder, err := newAVCodecDecoder(videoData)
	if err != nil {
		t.Fatalf("Failed to create decoder: %v", err)
	}
	defer decoder.Close()

	dstBuf := make([]byte, 10*1024*1024)
	options := &ImageOptions{
		FileType:                   ".webp",
		Width:                      200,
		Height:                     200,
		ResizeMethod:               ImageOpsFit,
		EncodeOptions:              map[int]int{WebpQuality: 75},
		VideoFrameSampleIntervalMs: 0, // No multi-frame extraction
		EncodeTimeout:              time.Second * 10,
	}

	ops := NewImageOps(2000)
	defer ops.Close()

	output, err := ops.Transform(decoder, options, dstBuf)
	if err != nil {
		t.Fatalf("Transform failed: %v", err)
	}

	if len(output) == 0 {
		t.Fatal("Transform returned empty output")
	}

	// Decode to verify it's a single frame
	outputDecoder, err := newWebpDecoder(output)
	if err != nil {
		t.Fatalf("Failed to decode output WebP: %v", err)
	}
	defer outputDecoder.Close()

	header, err := outputDecoder.Header()
	if err != nil {
		t.Fatalf("Failed to get header: %v", err)
	}

	if header.IsAnimated() {
		t.Errorf("Expected single frame, but output is animated with %d frames", header.NumFrames())
	}

	t.Logf("Successfully extracted single frame with zero interval, output size: %d bytes", len(output))
}

func testVideoToAnimatedWebPVerifyFrameCount(t *testing.T) {
	// Test that the frame count matches expectations based on duration and sample interval
	videoData, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("Failed to read test video: %v", err)
	}

	decoder, err := newAVCodecDecoder(videoData)
	if err != nil {
		t.Fatalf("Failed to create decoder: %v", err)
	}

	duration := decoder.Duration()
	decoder.Close()

	t.Logf("Video duration: %v", duration)

	// Re-create decoder for transformation
	decoder, err = newAVCodecDecoder(videoData)
	if err != nil {
		t.Fatalf("Failed to create decoder: %v", err)
	}
	defer decoder.Close()

	sampleIntervalMs := 2000
	expectedFrames := int(float64(duration.Milliseconds())/float64(sampleIntervalMs)) + 1

	dstBuf := make([]byte, 50*1024*1024)
	options := &ImageOptions{
		FileType:                   ".webp",
		Width:                      200,
		Height:                     200,
		ResizeMethod:               ImageOpsFit,
		EncodeOptions:              map[int]int{WebpQuality: 75},
		VideoFrameSampleIntervalMs: sampleIntervalMs,
		EncodeTimeout:              time.Second * 30,
	}

	ops := NewImageOps(2000)
	defer ops.Close()

	output, err := ops.Transform(decoder, options, dstBuf)
	if err != nil {
		t.Fatalf("Transform failed: %v", err)
	}

	// Verify frame count
	outputDecoder, err := newWebpDecoder(output)
	if err != nil {
		t.Fatalf("Failed to decode output WebP: %v", err)
	}
	defer outputDecoder.Close()

	header, err := outputDecoder.Header()
	if err != nil {
		t.Fatalf("Failed to get header: %v", err)
	}

	actualFrames := header.NumFrames()

	// Allow for small variance due to timing precision
	if actualFrames < expectedFrames-1 || actualFrames > expectedFrames+1 {
		t.Errorf("Expected approximately %d frames, got %d frames", expectedFrames, actualFrames)
	}

	t.Logf("Video duration: %v, sample interval: %dms, expected ~%d frames, got %d frames",
		duration, sampleIntervalMs, expectedFrames, actualFrames)
}

func testVideoToAnimatedWebPWithResizing(t *testing.T) {
	// Test different resize methods
	videoData, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("Failed to read test video: %v", err)
	}

	testCases := []struct {
		name         string
		width        int
		height       int
		resizeMethod ImageOpsSizeMethod
	}{
		{"Fit 200x200", 200, 200, ImageOpsFit},
		{"Resize 300x200", 300, 200, ImageOpsResize},
		{"Fit 100x100", 100, 100, ImageOpsFit},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, err := newAVCodecDecoder(videoData)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, 50*1024*1024)
			options := &ImageOptions{
				FileType:                   ".webp",
				Width:                      tc.width,
				Height:                     tc.height,
				ResizeMethod:               tc.resizeMethod,
				EncodeOptions:              map[int]int{WebpQuality: 75},
				VideoFrameSampleIntervalMs: 2000,
				MaxEncodeFrames:            3,
				EncodeTimeout:              time.Second * 30,
			}

			ops := NewImageOps(2000)
			defer ops.Close()

			output, err := ops.Transform(decoder, options, dstBuf)
			if err != nil {
				t.Fatalf("Transform failed: %v", err)
			}

			if len(output) == 0 {
				t.Fatal("Transform returned empty output")
			}

			// Verify dimensions
			outputDecoder, err := newWebpDecoder(output)
			if err != nil {
				t.Fatalf("Failed to decode output WebP: %v", err)
			}
			defer outputDecoder.Close()

			header, err := outputDecoder.Header()
			if err != nil {
				t.Fatalf("Failed to get header: %v", err)
			}

			// For Fit method, one dimension should match exactly
			if tc.resizeMethod == ImageOpsFit {
				if header.Width() != tc.width && header.Height() != tc.height {
					t.Errorf("Expected at least one dimension to match request (%dx%d), got %dx%d",
						tc.width, tc.height, header.Width(), header.Height())
				}
			}

			t.Logf("%s: output dimensions %dx%d, size: %d bytes",
				tc.name, header.Width(), header.Height(), len(output))
		})
	}
}

func testVideoToAnimatedWebPWithNonAlignedDimensions(t *testing.T) {
	// Test non-aligned dimensions to ensure stride handling works correctly
	videoData, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("Failed to read test video: %v", err)
	}

	testCases := []struct {
		name   string
		width  int
		height int
	}{
		{"199x199 (not aligned to 8)", 199, 199},
		{"301x201 (neither aligned)", 301, 201},
		{"150x100 (partially aligned)", 150, 100},
		{"333x333 (not aligned)", 333, 333},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, err := newAVCodecDecoder(videoData)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, 50*1024*1024)
			options := &ImageOptions{
				FileType:                   ".webp",
				Width:                      tc.width,
				Height:                     tc.height,
				ResizeMethod:               ImageOpsResize,
				EncodeOptions:              map[int]int{WebpQuality: 75},
				VideoFrameSampleIntervalMs: 2000,
				MaxEncodeFrames:            3,
				EncodeTimeout:              time.Second * 30,
			}

			ops := NewImageOps(2000)
			defer ops.Close()

			output, err := ops.Transform(decoder, options, dstBuf)
			if err != nil {
				t.Fatalf("Transform failed with %dx%d: %v", tc.width, tc.height, err)
			}

			if len(output) == 0 {
				t.Fatal("Transform returned empty output")
			}

			// Verify output is valid WebP
			outputDecoder, err := newWebpDecoder(output)
			if err != nil {
				t.Fatalf("Failed to decode output WebP: %v", err)
			}
			defer outputDecoder.Close()

			header, err := outputDecoder.Header()
			if err != nil {
				t.Fatalf("Failed to get header: %v", err)
			}

			// Verify dimensions match
			if header.Width() != tc.width || header.Height() != tc.height {
				t.Errorf("Expected dimensions %dx%d, got %dx%d",
					tc.width, tc.height, header.Width(), header.Height())
			}

			// Verify it's animated
			if !header.IsAnimated() {
				t.Errorf("Expected animated WebP, got static image")
			}

			t.Logf("Successfully created %dx%d animated WebP, %d frames, size: %d bytes",
				header.Width(), header.Height(), header.NumFrames(), len(output))
		})
	}
}
