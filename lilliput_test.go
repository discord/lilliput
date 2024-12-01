// Package lilliput resizes and encodes images from
// compressed images
package lilliput

import (
	"io/ioutil"
	"testing"
)

func TestNewDecoder(t *testing.T) {
	tests := []struct {
		name                 string
		sourceFilePath       string
		wantHeight           int
		wantWidth            int
		wantErr              bool
		wantNegativeDuration bool
		wantAnimated         bool
	}{
		{
			name:           "Standard MP4",
			sourceFilePath: "testdata/big_buck_bunny_480p_10s_std.mp4",
			wantHeight:     480,
			wantWidth:      853,
			wantErr:        false,
		},
		{
			name:           "Progressive Download MP4",
			sourceFilePath: "testdata/big_buck_bunny_480p_10s_web.mp4",
			wantHeight:     480,
			wantWidth:      853,
			wantErr:        false,
		},
		{
			name:           "Audio-only MP3",
			sourceFilePath: "testdata/tos-intro-3s.mp3",
			wantErr:        false,
		},
		{
			name:           "Audio-only OGG",
			sourceFilePath: "testdata/tos-intro-3s.ogg",
			wantErr:        false,
		},
		{
			name:                 "Audio-only AAC",
			sourceFilePath:       "testdata/tos-intro-3s.aac",
			wantErr:              false,
			wantNegativeDuration: true,
		},
		{
			name:           "Audio-only FLAC",
			sourceFilePath: "testdata/tos-intro-3s.flac",
			wantErr:        false,
		},
		{
			name:           "Audio-only WAV",
			sourceFilePath: "testdata/tos-intro-3s.wav",
			wantErr:        false,
		},
		{
			name:                 "WebP Image",
			sourceFilePath:       "testdata/tears_of_steel_icc.webp",
			wantWidth:            1920,
			wantHeight:           800,
			wantNegativeDuration: true,
		},
		{
			name:                 "Animated WebP",
			sourceFilePath:       "testdata/big_buck_bunny_720_5s.webp",
			wantWidth:            480,
			wantHeight:           270,
			wantNegativeDuration: false,
			wantAnimated:         true,
		},
		{
			name:                 "Ordinary WebP",
			sourceFilePath:       "testdata/tears_of_steel_icc.webp",
			wantWidth:            1920,
			wantHeight:           800,
			wantNegativeDuration: true,
			wantAnimated:         false,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			sourceFileData, err := ioutil.ReadFile(tt.sourceFilePath)
			if err != nil {
				t.Fatalf("Failed to read source file: %v", err)
			}
			dec, err := NewDecoder(sourceFileData)
			if (err != nil) != tt.wantErr {
				t.Errorf("NewDecoder() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			header, err := dec.Header()
			if err != nil {
				t.Errorf("Failed to get header: %v", err)
			}
			if header.Width() != tt.wantWidth {
				t.Errorf("Expected width to be %v, got %v", tt.wantWidth, header.Width())
			}
			if header.Height() != tt.wantHeight {
				t.Errorf("Expected height to be %v, got %v", tt.wantHeight, header.Height())
			}
			if tt.wantNegativeDuration && dec.Duration() > 0 {
				t.Errorf("Expected duration to be less than 0, got %v", dec.Duration())
			}
			if !tt.wantNegativeDuration && dec.Duration() <= 0 {
				t.Errorf("Expected duration to be greater than 0, got %v", dec.Duration())
			}
			if !tt.wantAnimated && header.IsAnimated() {
				t.Errorf("Expected image to not be animated")
			}
			if tt.wantAnimated && !header.IsAnimated() {
				t.Errorf("Expected image to be animated")
			}
		})
	}
}

func BenchmarkNewDecoder(b *testing.B) {
	sourceFilePath := "testdata/big_buck_bunny_480p_10s_web.mp4"
	sourceFileData, err := ioutil.ReadFile(sourceFilePath)
	if err != nil {
		b.Fatalf("Failed to read source file: %v", err)
	}

	b.ResetTimer() // Start timing after setup

	for i := 0; i < b.N; i++ {
		dec, err := NewDecoder(sourceFileData)
		if err != nil {
			b.Fatalf("Failed to create decoder: %v", err)
		}
		if dec != nil {
			defer dec.Close()
		}
		if _, err := dec.Header(); err != nil {
			b.Fatalf("Failed to get header: %v", err)
		}
	}
}
