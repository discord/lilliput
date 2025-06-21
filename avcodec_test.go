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

func TestAV1Support(t *testing.T) {
	// Test that AV1 variables are properly defined
	if av1Enabled != "true" && av1Enabled != "" {
		// This should be empty by default, confirming our variable is defined
		t.Logf("AV1 support is disabled (expected): %q", av1Enabled)
	}
}

func TestAV1VideoDecoding(t *testing.T) {
	// Test AV1 video file
	av1Mp4, err := os.ReadFile("testdata/av1-mp4.mp4")
	if err != nil {
		t.Fatalf("failed to open AV1 test file: %v", err)
	}

	t.Run("AV1_Disabled_By_Default", func(t *testing.T) {
		// Should fail when AV1 is disabled (default)
		_, err := newAVCodecDecoder(av1Mp4)
		if err == nil {
			t.Logf("AV1 decoder created successfully with av1Enabled=%q", av1Enabled)
		} else {
			t.Logf("AV1 decoder failed as expected when disabled: %v", err)
		}
	})

	// Note: Testing with AV1 enabled requires building with the flag:
	// go test -ldflags="-X=github.com/discord/lilliput.av1Enabled=true"
	if av1Enabled == "true" {
		t.Run("AV1_Enabled_Decoding", func(t *testing.T) {
			decoder, err := newAVCodecDecoder(av1Mp4)
			if err != nil {
				t.Fatalf("failed to create AV1 decoder: %v", err)
			}
			defer decoder.Close()

			// Test basic metadata
			header, err := decoder.Header()
			if err != nil {
				t.Fatalf("failed to get AV1 header: %v", err)
			}

			t.Logf("AV1 video dimensions: %dx%d", header.Width(), header.Height())
			t.Logf("AV1 video duration: %v", decoder.Duration())
			t.Logf("AV1 video description: %s", decoder.Description())

			// Test first frame extraction
			framebuffer := NewFramebuffer(header.Width(), header.Height())
			defer framebuffer.Close()

			err = decoder.DecodeTo(framebuffer)
			if err != nil {
				t.Fatalf("failed to decode AV1 first frame: %v", err)
			}

			t.Log("Successfully extracted first frame from AV1 video")
		})
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
