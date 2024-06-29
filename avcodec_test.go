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
	if isStreamable(createMatFromBytes(stdMp4)) {
		t.Fatalf("expected file to not be streamable")
	}

	// Web-optimized streamable MP4
	webMp4, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	if !isStreamable(createMatFromBytes(webMp4)) {
		t.Fatalf("expected file to be streamable")
	}

	// MP4 with a big atom
	bigAtomMp4, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_big_atom.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	if isStreamable(createMatFromBytes(bigAtomMp4)) {
		t.Fatalf("expected file to not be streamable")
	}

	// MP4 with a zero-length atom
	zeroLengthAtomMp4, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_zero_length_atom.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	if isStreamable(createMatFromBytes(zeroLengthAtomMp4)) {
		t.Fatalf("expected file to not be streamable")
	}
}

func TestICCProfile(t *testing.T) {
	// Web-optimized streamable MP4 using BT.709 color space
	webMp4, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_web.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	webAvCodecDecoder, err := newAVCodecDecoder(webMp4)
	if err != nil {
		t.Fatalf("failed to create decoder: %v", err)
	}
	defer webAvCodecDecoder.Close()
	if len(webAvCodecDecoder.ICC()) == 0 {
		t.Fatalf("expected ICC profile")
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
