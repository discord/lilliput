package lilliput

import (
	"bytes"
	"encoding/binary"
	"hash/crc32"
	"os"
	"testing"
	"time"
)

// injectPNGCICP inserts a cICP chunk directly after IHDR, which is where PNG
// 3rd edition requires colour chunks to sit. The OpenCV encoder cannot emit
// one, so test fixtures are assembled by hand.
func injectPNGCICP(t *testing.T, src []byte, primaries, transfer uint8) []byte {
	t.Helper()
	if !bytes.HasPrefix(src, []byte("\x89PNG\r\n\x1a\n")) {
		t.Fatal("fixture is not a PNG")
	}
	body := []byte{primaries, transfer, 0, 1}
	chunk := make([]byte, 0, 16)
	chunk = binary.BigEndian.AppendUint32(chunk, uint32(len(body)))
	chunk = append(chunk, []byte("cICP")...)
	chunk = append(chunk, body...)
	chunk = binary.BigEndian.AppendUint32(chunk, crc32.ChecksumIEEE(chunk[4:]))

	ihdrLen := binary.BigEndian.Uint32(src[8:12])
	insertAt := 8 + 12 + int(ihdrLen)

	out := make([]byte, 0, len(src)+len(chunk))
	out = append(out, src[:insertAt]...)
	out = append(out, chunk...)
	out = append(out, src[insertAt:]...)
	return out
}

func pngChunkTypes(t *testing.T, png []byte) []string {
	t.Helper()
	var types []string
	for i := 8; i+8 <= len(png); {
		size := int(binary.BigEndian.Uint32(png[i : i+4]))
		types = append(types, string(png[i+4:i+8]))
		i += size + 12
	}
	return types
}

func hasChunk(types []string, want string) bool {
	for _, t := range types {
		if t == want {
			return true
		}
	}
	return false
}

func transformPNG(t *testing.T, in []byte) []byte {
	t.Helper()
	d, err := NewDecoder(in)
	if err != nil {
		t.Fatalf("decoder: %v", err)
	}
	defer d.Close()
	h, err := d.Header()
	if err != nil {
		t.Fatalf("header: %v", err)
	}

	ops := NewImageOps(8192)
	defer ops.Close()
	opts := &ImageOptions{
		FileType:      ".png",
		Width:         h.Width(),
		Height:        h.Height(),
		ResizeMethod:  ImageOpsFit,
		EncodeOptions: map[int]int{PngCompression: 7},
		EncodeTimeout: time.Minute,
	}
	out, err := ops.Transform(d, opts, make([]byte, 16*1024*1024))
	if err != nil {
		t.Fatalf("transform: %v", err)
	}
	return out
}

// A cICP chunk with an SDR transfer is signalling only: the pixels are already
// displayable, so they must pass through untouched and the chunk must survive
// to the output. Before the cICP fix it was dropped unconditionally.
func TestPNGSDRCICPRoundTrips(t *testing.T) {
	src, err := os.ReadFile("testdata/ferry_sunset_no_icc.png")
	if err != nil {
		t.Skipf("fixture unavailable: %v", err)
	}
	in := injectPNGCICP(t, src, 12, 13) // Display-P3 primaries, sRGB transfer

	d, err := NewDecoder(in)
	if err != nil {
		t.Fatalf("decoder: %v", err)
	}
	cicpSource, ok := d.(interface{ CICP() (CICP, bool) })
	if !ok {
		d.Close()
		t.Fatal("PNG decoder must expose CICP()")
	}
	cicp, present := cicpSource.CICP()
	d.Close()
	if !present {
		t.Fatal("cICP chunk was not read off the source")
	}
	if cicp.Primaries != 12 || cicp.Transfer != 13 {
		t.Fatalf("unexpected cICP: %+v", cicp)
	}
	if cicp.IsHDR() {
		t.Fatal("sRGB transfer must not be treated as HDR")
	}

	types := pngChunkTypes(t, transformPNG(t, in))
	if !hasChunk(types, "cICP") {
		t.Fatalf("output must carry the cICP chunk, got %v", types)
	}
}

// PQ transfer at 8-bit depth is legal per PNG 3rd edition and is the case an
// AVIF-style `depth > 8` HDR test would miss. The pixels must be tone-mapped
// and the now-inapplicable tag must not be written to the output.
func TestPNGHDRCICPIsTonemapped(t *testing.T) {
	src, err := os.ReadFile("testdata/ferry_sunset_no_icc.png")
	if err != nil {
		t.Skipf("fixture unavailable: %v", err)
	}
	in := injectPNGCICP(t, src, 9, 16) // BT.2020 primaries, PQ transfer

	untagged := transformPNG(t, src)
	tonemapped := transformPNG(t, in)

	if bytes.Equal(untagged, tonemapped) {
		t.Fatal("PQ source must be tone-mapped, not passed through unchanged")
	}
	types := pngChunkTypes(t, tonemapped)
	if hasChunk(types, "cICP") {
		t.Fatalf("tone-mapped output must not carry a PQ cICP tag, got %v", types)
	}
}

// Control: a PNG with no cICP chunk must be entirely unaffected by the fix.
func TestPNGWithoutCICPUnchanged(t *testing.T) {
	src, err := os.ReadFile("testdata/ferry_sunset_no_icc.png")
	if err != nil {
		t.Skipf("fixture unavailable: %v", err)
	}
	types := pngChunkTypes(t, transformPNG(t, src))
	if hasChunk(types, "cICP") {
		t.Fatalf("plain PNG must not gain a cICP chunk, got %v", types)
	}
}
