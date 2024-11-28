package lilliput

import (
	"io"
	"os"
	"testing"
	"time"
)

func TestGIFOperations(t *testing.T) {
	t.Run("GIFDuration", testGIFDuration)
}

func testGIFDuration(t *testing.T) {
	testCases := []struct {
		name          string
		filename      string
		wantLoopCount int
		wantFrames    int
		wantDuration  time.Duration
		description   string
	}{
		{
			name:          "Standard animated GIF",
			filename:      "testdata/party-discord.gif",
			wantLoopCount: 0, // infinite loop
			wantFrames:    16,
			wantDuration:  time.Millisecond * 480,
			description:   "Basic animation with custom delays",
		},
		{
			name:          "Static GIF image",
			filename:      "testdata/ferry_sunset.gif",
			wantLoopCount: 1, // play once
			wantFrames:    1,
			wantDuration:  0,
			description:   "Static image, no animation",
		},
		{
			name:          "Single loop GIF",
			filename:      "testdata/no-loop.gif",
			wantLoopCount: 1, // play once
			wantFrames:    44,
			wantDuration:  time.Millisecond * 4400,
			description:   "Animation that plays only once",
		},
		{
			name:          "Duplicate loop count GIF",
			filename:      "testdata/duplicate_number_of_loops.gif",
			wantLoopCount: 2, // play twice
			wantFrames:    2,
			wantDuration:  0, // unable to determine duration
			description:   "Animation with duplicate NETSCAPE2.0 extension blocks",
		},
		{
			name:          "Background dispose GIF",
			filename:      "testdata/dispose_bgnd.gif",
			wantLoopCount: 0, // infinite loop
			wantFrames:    5,
			wantDuration:  time.Second * 5,
			description:   "Animation with background disposal method",
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testGIFImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Failed to read gif image: %v", err)
			}

			decoder, err := newGifDecoder(testGIFImage)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			// Test loop count
			if got := decoder.LoopCount(); got != tc.wantLoopCount {
				t.Errorf("LoopCount() = %v, want %v", got, tc.wantLoopCount)
			}

			// Test frame count
			if got := decoder.FrameCount(); got != tc.wantFrames {
				t.Errorf("FrameCount() = %v, want %v", got, tc.wantFrames)
			}

			// Test total duration
			if got := decoder.Duration(); got != tc.wantDuration {
				t.Errorf("Duration() = %v, want %v (%s)", got, tc.wantDuration, tc.description)
			}

			// Test per-frame durations
			header, err := decoder.Header()
			if err != nil {
				t.Fatalf("Failed to get header: %v", err)
			}

			framebuffer := NewFramebuffer(header.width, header.height)
			defer framebuffer.Close()

			var totalDuration time.Duration
			frameCount := 0
			for {
				err = decoder.DecodeTo(framebuffer)
				if err == io.EOF {
					break
				}
				if err != nil {
					t.Fatalf("DecodeTo failed: %v", err)
				}

				totalDuration += framebuffer.Duration()
				frameCount++
			}

			// Verify total duration matches sum of frame durations
			if totalDuration != tc.wantDuration {
				t.Errorf("Sum of frame durations (%v) doesn't match total duration (%v)",
					totalDuration, tc.wantDuration)
			}
		})
	}
}
