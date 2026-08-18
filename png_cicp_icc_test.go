package lilliput

import (
	"os"
	"testing"
	"time"
)

func transformTo(t *testing.T, in []byte, fileType string) []byte {
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
		FileType:      fileType,
		Width:         h.Width(),
		Height:        h.Height(),
		ResizeMethod:  ImageOpsFit,
		EncodeOptions: map[int]int{WebpQuality: 85, AvifQuality: 60, AvifSpeed: 10},
		EncodeTimeout: time.Minute,
	}
	out, err := ops.Transform(d, opts, make([]byte, 32*1024*1024))
	if err != nil {
		t.Fatalf("transform to %s: %v", fileType, err)
	}
	return out
}

// A PNG whose colour is signalled only by cICP must not lose that signalling
// when re-encoded into a format with no cICP channel. WebP carries an ICCP
// chunk, so the primaries are synthesized into a real profile instead.
func TestPNGCICPCarriedIntoWebPAsICC(t *testing.T) {
	src, err := os.ReadFile("testdata/ferry_sunset_no_icc.png")
	if err != nil {
		t.Skipf("fixture unavailable: %v", err)
	}
	// Display-P3 primaries with an sRGB transfer: wide gamut, not HDR.
	in := injectPNGCICP(t, src, 12, 13)

	out := transformTo(t, in, ".webp")
	d, err := NewDecoder(out)
	if err != nil {
		t.Fatalf("decode webp output: %v", err)
	}
	defer d.Close()
	if _, err := d.Header(); err != nil {
		t.Fatalf("webp header: %v", err)
	}
	icc := d.ICC()
	if len(icc) == 0 {
		t.Fatal("WebP output must carry an ICC profile synthesized from the source cICP")
	}
	if !ICCHeaderIsSane(icc) {
		t.Fatalf("synthesized profile is malformed (len %d)", len(icc))
	}
	want := CICP{Primaries: 12}.SynthesizeICC()
	if len(want) == 0 {
		t.Fatal("expected a canned P3 profile for primaries 12")
	}
	if string(icc) != string(want) {
		t.Fatalf("WebP ICC is not the P3 profile the cICP primaries map to (got %d bytes, want %d)", len(icc), len(want))
	}
}

// cICP takes precedence over iCCP per PNG 3rd edition, so a source carrying
// both must resolve to the cICP-derived profile on a WebP sink, replacing
// rather than merging with the embedded one.
func TestPNGCICPBeatsICCPOnWebP(t *testing.T) {
	src, err := os.ReadFile("testdata/ferry_sunset.png")
	if err != nil {
		t.Skipf("fixture unavailable: %v", err)
	}
	sourceICC := func() []byte {
		d, err := NewDecoder(src)
		if err != nil {
			t.Fatalf("decoder: %v", err)
		}
		defer d.Close()
		if _, err := d.Header(); err != nil {
			t.Fatalf("header: %v", err)
		}
		return append([]byte(nil), d.ICC()...)
	}()
	if len(sourceICC) == 0 {
		t.Skip("fixture has no iCCP to be overridden")
	}

	in := injectPNGCICP(t, src, 9, 13) // BT.2020 primaries, SDR transfer
	out := transformTo(t, in, ".webp")

	d, err := NewDecoder(out)
	if err != nil {
		t.Fatalf("decode webp output: %v", err)
	}
	defer d.Close()
	if _, err := d.Header(); err != nil {
		t.Fatalf("webp header: %v", err)
	}
	icc := d.ICC()
	want := CICP{Primaries: 9}.SynthesizeICC()
	if string(icc) == string(sourceICC) {
		t.Fatal("cICP must take precedence over the source iCCP, but the source profile was kept")
	}
	if string(icc) != string(want) {
		t.Fatalf("expected the Rec2020 profile from cICP primaries 9, got %d bytes", len(icc))
	}
}

// The primaries mapping must add P3 without disturbing the video path's
// mapping, which has no P3 entry and must stay byte-for-byte as it was.
func TestCICPPrimariesMapping(t *testing.T) {
	p3 := CICP{Primaries: 12}.SynthesizeICC()
	dciP3 := CICP{Primaries: 11}.SynthesizeICC()
	if string(dciP3) != string(p3) {
		t.Fatal("DCI-P3 (11) and Display-P3 (12) must map to the same canned profile")
	}
	srgb := CICP{Primaries: 1}.SynthesizeICC()
	unspec := CICP{Primaries: 2}.SynthesizeICC()
	if string(p3) == string(srgb) {
		t.Fatal("P3 primaries must not fall back to sRGB")
	}
	for _, pr := range []uint8{9, 5, 6} {
		got := CICP{Primaries: pr}.SynthesizeICC()
		if len(got) == 0 || string(got) == string(p3) {
			t.Fatalf("primaries %d must map to its own non-P3 profile", pr)
		}
	}
	if string(unspec) != string(srgb) {
		t.Fatal("unspecified primaries must fall back to sRGB")
	}
}

// A malformed profile must be dropped rather than muxed, since a corrupt ICCP
// chunk can fail the decode outright while untagged output still renders.
func TestMalformedICCIsRejected(t *testing.T) {
	if ICCHeaderIsSane(nil) {
		t.Fatal("empty blob must not be sane")
	}
	if ICCHeaderIsSane(make([]byte, 64)) {
		t.Fatal("sub-header-length blob must not be sane")
	}
	bad := make([]byte, 200) // declared size 0, actual 200
	if ICCHeaderIsSane(bad) {
		t.Fatal("size-field mismatch must not be sane")
	}
	good := CICP{Primaries: 12}.SynthesizeICC()
	if !ICCHeaderIsSane(good) {
		t.Fatal("canned profile must pass the header check")
	}
	truncated := good[:len(good)-8]
	if ICCHeaderIsSane(truncated) {
		t.Fatal("truncated profile must not be sane")
	}
}
