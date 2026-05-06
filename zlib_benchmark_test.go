package lilliput

import (
	"os"
	"testing"
)

func benchmarkPngDecode(b *testing.B, path string) {
	data, err := os.ReadFile(path)
	if err != nil {
		b.Fatalf("read %s: %v", path, err)
	}
	b.SetBytes(int64(len(data)))
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		dec, err := newOpenCVDecoder(data)
		if err != nil {
			b.Fatalf("decoder: %v", err)
		}
		hdr, err := dec.Header()
		if err != nil {
			b.Fatalf("header: %v", err)
		}
		fb := NewFramebuffer(hdr.width, hdr.height)
		if err := dec.DecodeTo(fb); err != nil {
			b.Fatalf("decode: %v", err)
		}
		fb.Close()
		dec.Close()
	}
}

func benchmarkPngEncode(b *testing.B, path string, compression int) {
	data, err := os.ReadFile(path)
	if err != nil {
		b.Fatalf("read %s: %v", path, err)
	}
	dec, err := newOpenCVDecoder(data)
	if err != nil {
		b.Fatalf("decoder: %v", err)
	}
	defer dec.Close()
	hdr, err := dec.Header()
	if err != nil {
		b.Fatalf("header: %v", err)
	}
	fb := NewFramebuffer(hdr.width, hdr.height)
	defer fb.Close()
	if err := dec.DecodeTo(fb); err != nil {
		b.Fatalf("decode: %v", err)
	}

	dst := make([]byte, destinationBufferSize)
	opts := map[int]int{PngCompression: compression}

	b.ResetTimer()
	var lastSize int64
	for i := 0; i < b.N; i++ {
		enc, err := newOpenCVEncoder(".png", dec, dst, nil)
		if err != nil {
			b.Fatalf("encoder: %v", err)
		}
		out, err := enc.Encode(fb, opts)
		if err != nil {
			b.Fatalf("encode: %v", err)
		}
		lastSize = int64(len(out))
		enc.Close()
	}
	b.ReportMetric(float64(lastSize), "out_bytes/op")
}

func BenchmarkPNGDecode(b *testing.B) {
	b.Run("ferry_sunset", func(b *testing.B) { benchmarkPngDecode(b, "testdata/ferry_sunset.png") })
}

func BenchmarkPNGEncode(b *testing.B) {
	for _, lvl := range []int{1, 6, 9} {
		lvl := lvl
		b.Run("ferry_sunset_lvl"+itoa(lvl), func(b *testing.B) {
			benchmarkPngEncode(b, "testdata/ferry_sunset.png", lvl)
		})
	}
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var buf [4]byte
	i := len(buf)
	for n > 0 {
		i--
		buf[i] = byte('0' + n%10)
		n /= 10
	}
	return string(buf[i:])
}
