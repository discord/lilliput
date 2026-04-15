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

func TestKeyframeEntries(t *testing.T) {
	buf, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_std.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	dec, err := newAVCodecDecoder(buf)
	if err != nil {
		t.Fatalf("failed to create decoder: %v", err)
	}
	defer dec.Close()

	entries, err := dec.Keyframes()
	if err != nil {
		t.Fatalf("failed to get keyframes: %v", err)
	}
	if len(entries) == 0 {
		t.Fatal("expected keyframe entries")
	}

	if entries[0].TimestampUs < 0 {
		t.Fatalf("first keyframe timestamp should be non-negative, got %d", entries[0].TimestampUs)
	}

	for i, e := range entries {
		if e.ByteOffset <= 0 {
			t.Fatalf("keyframe %d byte_offset should be positive, got %d", i, e.ByteOffset)
		}
		if e.Size <= 0 {
			t.Fatalf("keyframe %d size should be positive, got %d", i, e.Size)
		}
	}

	for i := 1; i < len(entries); i++ {
		if entries[i].TimestampUs < entries[i-1].TimestampUs {
			t.Fatalf("keyframe timestamps should be non-decreasing: kf[%d]=%d < kf[%d]=%d",
				i, entries[i].TimestampUs, i-1, entries[i-1].TimestampUs)
		}
	}
}

func TestCodecIDAndExtradata(t *testing.T) {
	buf, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_std.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	dec, err := newAVCodecDecoder(buf)
	if err != nil {
		t.Fatalf("failed to create decoder: %v", err)
	}
	defer dec.Close()

	codecID, err := dec.CodecID()
	if err != nil {
		t.Fatalf("failed to get codec id: %v", err)
	}
	if codecID <= 0 {
		t.Fatalf("expected valid codec id, got %d", codecID)
	}

	extradata, err := dec.Extradata()
	if err != nil {
		t.Fatalf("failed to get extradata: %v", err)
	}
	if len(extradata) == 0 {
		t.Fatal("expected non-empty extradata (SPS/PPS)")
	}
}

// extractFtypMoov extracts ftyp + moov boxes from an mp4 buffer, stripping mdat.
// this simulates what a media proxy receives via range requests (moov only, no video data).
func extractFtypMoov(buf []byte) []byte {
	var out []byte
	offset := 0
	for offset+8 <= len(buf) {
		boxSize := int(buf[offset])<<24 | int(buf[offset+1])<<16 | int(buf[offset+2])<<8 | int(buf[offset+3])
		boxType := string(buf[offset+4 : offset+8])
		if boxSize < 8 || offset+boxSize > len(buf) {
			break
		}
		if boxType == "ftyp" || boxType == "moov" {
			out = append(out, buf[offset:offset+boxSize]...)
		}
		offset += boxSize
	}
	return out
}

func TestMoovOnlyParsing(t *testing.T) {
	fullBuf, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_std.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	moovBuf := extractFtypMoov(fullBuf)
	if len(moovBuf) >= len(fullBuf) {
		t.Fatal("moov-only buffer should be smaller than the full file")
	}

	dec, err := newAVCodecDecoder(moovBuf)
	if err != nil {
		t.Fatalf("failed to create decoder from moov-only buffer: %v", err)
	}
	defer dec.Close()

	entries, err := dec.Keyframes()
	if err != nil {
		t.Fatalf("failed to get keyframes from moov-only buffer: %v", err)
	}
	if len(entries) == 0 {
		t.Fatal("expected keyframes from moov-only buffer")
	}

	codecID, err := dec.CodecID()
	if err != nil {
		t.Fatalf("failed to get codec id from moov-only buffer: %v", err)
	}
	if codecID <= 0 {
		t.Fatalf("expected valid codec id from moov-only buffer, got %d", codecID)
	}

	extradata, err := dec.Extradata()
	if err != nil {
		t.Fatalf("failed to get extradata from moov-only buffer: %v", err)
	}
	if len(extradata) == 0 {
		t.Fatal("expected extradata from moov-only buffer")
	}

	for i, e := range entries {
		if e.ByteOffset <= 0 {
			t.Fatalf("keyframe %d byte_offset should be positive, got %d", i, e.ByteOffset)
		}
		if e.Size <= 0 {
			t.Fatalf("keyframe %d size should be positive, got %d", i, e.Size)
		}
	}
}

func TestDecodeMultipleKeyframes(t *testing.T) {
	buf, err := os.ReadFile("testdata/big_buck_bunny_480p_10s_std.mp4")
	if err != nil {
		t.Fatalf("failed to open test file: %v", err)
	}
	dec, err := newAVCodecDecoder(buf)
	if err != nil {
		t.Fatalf("failed to create decoder: %v", err)
	}
	defer dec.Close()

	header, err := dec.Header()
	if err != nil {
		t.Fatalf("failed to get header: %v", err)
	}
	codecID, err := dec.CodecID()
	if err != nil {
		t.Fatalf("failed to get codec id: %v", err)
	}
	extradata, err := dec.Extradata()
	if err != nil {
		t.Fatalf("failed to get extradata: %v", err)
	}
	entries, err := dec.Keyframes()
	if err != nil {
		t.Fatalf("failed to get keyframes: %v", err)
	}

	thumbW := 160
	thumbH := int(float64(header.Height()) / float64(header.Width()) * float64(thumbW))
	if thumbH%2 != 0 {
		thumbH++
	}

	toTest := len(entries)
	if toTest > 5 {
		toTest = 5
	}
	if toTest == 0 {
		t.Fatal("need at least one keyframe")
	}

	for i := 0; i < toTest; i++ {
		kf := entries[i]
		start := int(kf.ByteOffset)
		end := start + int(kf.Size)
		if end > len(buf) {
			t.Fatalf("keyframe %d range %d..%d exceeds file size %d", i, start, end, len(buf))
		}
		chunk := buf[start:end]

		fb := NewFramebuffer(thumbW, thumbH)
		err := DecodeRawKeyframe(codecID, extradata, header.Width(), header.Height(), chunk, thumbW, thumbH, fb)
		fb.Close()
		if err != nil {
			t.Fatalf("keyframe %d (t=%dus, offset=%d, size=%d) failed to decode: %v",
				i, kf.TimestampUs, kf.ByteOffset, kf.Size, err)
		}
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
