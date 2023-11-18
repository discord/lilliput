package lilliput

import (
	"os"
	"testing"
)

func TestIsStreamable(t *testing.T) {
	// Standard MP4
	stdMp4, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_std.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}

	stdAvCodecDecoder, err := newAVCodecDecoder(stdMp4)
	if err != nil {
		t.Fatalf("failed to create decoder: %v", err)
	}
	defer stdAvCodecDecoder.Close()

	if stdAvCodecDecoder.IsStreamable() {
		t.Fatalf("expected file to not be streamable")
	}

	// Web-optimized streamable MP4
	webMp4, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}

	webAvCodecDecoder, err := newAVCodecDecoder(webMp4)
	if err != nil {
		t.Fatalf("failed to create decoder: %v", err)
	}
	defer webAvCodecDecoder.Close()

	if !webAvCodecDecoder.IsStreamable() {
		t.Fatalf("expected file to be streamable")
	}
}

func BenchmarkIsStreamableWebMp4(b *testing.B) {
	// Read the web-optimized streamable MP4 file
	webMp4, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		b.Fatalf("failed to open test file: %v", err)
	}
	b.ResetTimer()

	// Run the benchmark
	for i := 0; i < b.N; i++ {
		webAvCodecDecoder, err := newAVCodecDecoder(webMp4)
		if err != nil {
			b.Fatalf("failed to create decoder: %v", err)
		}
		defer webAvCodecDecoder.Close()
		_ = webAvCodecDecoder.IsStreamable()
	}
}
