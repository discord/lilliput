package lilliput

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"testing"
)

const (
	jpegDestinationBufferSize = 10 * 1024 * 1024
)

func TestJpegEncoderBufferTooSmall(t *testing.T) {
	testCases := []struct {
		name         string
		filePath     string
		bufferSize   int
		options      map[int]int
		expectErrBuf bool
	}{
		{
			name:         "100 bytes",
			filePath:     "testdata/ferry_sunset.jpg",
			bufferSize:   100,
			options:      map[int]int{JpegQuality: 90},
			expectErrBuf: true,
		},
		{
			name:         "1 KiB",
			filePath:     "testdata/ferry_sunset.jpg",
			bufferSize:   1024,
			options:      map[int]int{JpegQuality: 90},
			expectErrBuf: true,
		},
		{
			name:         "10 KiB",
			filePath:     "testdata/ferry_sunset.jpg",
			bufferSize:   10 * 1024,
			options:      map[int]int{JpegQuality: 90},
			expectErrBuf: true,
		},
		{
			name:         "10 MiB",
			filePath:     "testdata/ferry_sunset.jpg",
			bufferSize:   10 * 1024 * 1024,
			options:      map[int]int{JpegQuality: 90},
			expectErrBuf: false,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, framebuffer := decodeTestImage(t, tc.filePath)

			// Create JPEG encoder with specific buffer size
			dstBuf := make([]byte, tc.bufferSize)
			encoder, err := newJpegEncoder(decoder, dstBuf)
			if err != nil {
				t.Fatalf("Failed to create encoder: %v", err)
			}
			defer encoder.Close()

			encodedData, err := encoder.Encode(framebuffer, tc.options)

			if tc.expectErrBuf {
				// Should get ErrBufTooSmall
				if !errors.Is(err, ErrBufTooSmall) {
					t.Errorf("Expected ErrBufTooSmall with %d byte buffer, got: %v", tc.bufferSize, err)
				}
			} else {
				// Should succeed
				if err != nil {
					t.Errorf("Expected success with %d byte buffer, got error: %v", tc.bufferSize, err)
				}
				if len(encodedData) == 0 {
					t.Errorf("Encoded data is empty despite no error")
				}
				if len(encodedData) > tc.bufferSize {
					t.Errorf("Encoded data size %d exceeds buffer size %d", len(encodedData), tc.bufferSize)
				}
			}
		})
	}
}

func TestJpegEncoderInvalidFramebuffer(t *testing.T) {
	t.Run("Nil framebuffer", func(t *testing.T) {
		dstBuf := make([]byte, jpegDestinationBufferSize)
		encoder, err := newJpegEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{JpegQuality: 90}
		_, err = encoder.Encode(nil, options)
		if err != io.EOF {
			t.Errorf("Expected io.EOF with nil framebuffer, got: %v", err)
		}
	})

	t.Run("Zero-dimension framebuffer (0x0)", func(t *testing.T) {
		framebuffer := NewFramebuffer(0, 0)
		defer framebuffer.Close()

		dstBuf := make([]byte, jpegDestinationBufferSize)
		encoder, err := newJpegEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{JpegQuality: 90}
		_, err = encoder.Encode(framebuffer, options)
		if !errors.Is(err, ErrFrameBufNoPixels) {
			t.Errorf("Expected ErrFrameBufNoPixels with 0x0 framebuffer, got: %v", err)
		}
	})

	t.Run("Zero width framebuffer (0x100)", func(t *testing.T) {
		framebuffer := NewFramebuffer(0, 100)
		defer framebuffer.Close()

		dstBuf := make([]byte, jpegDestinationBufferSize)
		encoder, err := newJpegEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{JpegQuality: 90}
		_, err = encoder.Encode(framebuffer, options)
		if !errors.Is(err, ErrFrameBufNoPixels) {
			t.Errorf("Expected ErrFrameBufNoPixels with 0x100 framebuffer, got: %v", err)
		}
	})

	t.Run("Zero height framebuffer (100x0)", func(t *testing.T) {
		framebuffer := NewFramebuffer(100, 0)
		defer framebuffer.Close()

		dstBuf := make([]byte, jpegDestinationBufferSize)
		encoder, err := newJpegEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{JpegQuality: 90}
		_, err = encoder.Encode(framebuffer, options)
		if !errors.Is(err, ErrFrameBufNoPixels) {
			t.Errorf("Expected ErrFrameBufNoPixels with 100x0 framebuffer, got: %v", err)
		}
	})
}

func TestJpegEncoderSmallImages(t *testing.T) {
	framebuffer := createInitializedFramebuffer(t, 1, 1)

	dstBuf := make([]byte, 10*1024)
	encoder, err := newJpegEncoder(nil, dstBuf)
	if err != nil {
		t.Fatalf("Failed to create encoder: %v", err)
	}
	defer encoder.Close()

	options := map[int]int{JpegQuality: 90}
	encodedData, err := encoder.Encode(framebuffer, options)
	if err != nil {
		t.Fatalf("Encode failed unexpectedly: %v", err)
	}

	if len(encodedData) == 0 {
		t.Errorf("Encoded data is empty")
	}

	// JPEG minimum is over 100 bytes
	minSize := 100
	if len(encodedData) < minSize {
		t.Errorf("Encoded JPEG 1x1 image is %d bytes, expected at least %d bytes", len(encodedData), minSize)
	}

	t.Logf("1x1 pixel .jpeg encoded to %d bytes", len(encodedData))
}

func TestJpegEncoderInvalidParameters(t *testing.T) {
	decoder, framebuffer := decodeTestImage(t, "testdata/ferry_sunset.jpg")

	testCases := []struct {
		name           string
		invalidOptions map[int]int
		clampedOptions map[int]int
	}{
		{
			name:           "Negative quality clamped to 1",
			invalidOptions: map[int]int{JpegQuality: -1},
			clampedOptions: map[int]int{JpegQuality: 1},
		},
		{
			name:           "Zero quality clamped to 1",
			invalidOptions: map[int]int{JpegQuality: 0},
			clampedOptions: map[int]int{JpegQuality: 1},
		},
		{
			name:           "Over 100 quality clamped to 100",
			invalidOptions: map[int]int{JpegQuality: 150},
			clampedOptions: map[int]int{JpegQuality: 100},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Encode with invalid quality (should be clamped by library)
			dstBuf1 := make([]byte, jpegDestinationBufferSize)
			encoder1, err := newJpegEncoder(decoder, dstBuf1)
			if err != nil {
				t.Fatalf("Failed to create encoder: %v", err)
			}
			defer encoder1.Close()

			invalidData, err := encoder1.Encode(framebuffer, tc.invalidOptions)
			if err != nil {
				t.Fatalf("Encoding with invalid parameter failed: %v", err)
			}

			// Encode with explicitly clamped quality for comparison
			dstBuf2 := make([]byte, jpegDestinationBufferSize)
			encoder2, err := newJpegEncoder(decoder, dstBuf2)
			if err != nil {
				t.Fatalf("Failed to create encoder: %v", err)
			}
			defer encoder2.Close()

			clampedData, err := encoder2.Encode(framebuffer, tc.clampedOptions)
			if err != nil {
				t.Fatalf("Encoding with clamped parameter failed: %v", err)
			}

			// Verify outputs are byte-identical
			if !bytes.Equal(invalidData, clampedData) {
				t.Errorf("Expected library to clamp quality value, but outputs differ (invalid: %d bytes, clamped: %d bytes)",
					len(invalidData), len(clampedData))
			}
		})
	}
}

func TestJpegQualityFileSize(t *testing.T) {
	decoder, framebuffer := decodeTestImage(t, "testdata/ferry_sunset.jpg")

	dstBuf := make([]byte, jpegDestinationBufferSize)
	encoder, err := newJpegEncoder(decoder, dstBuf)
	if err != nil {
		t.Fatalf("Failed to create encoder: %v", err)
	}
	defer encoder.Close()

	// Encode with alternating quality: 90, 10, 90, 10
	qualities := []int{90, 10, 90, 10}
	var sizes [4]int

	for i := 0; i < 4; i++ {
		encodedData, err := encoder.Encode(framebuffer, map[int]int{JpegQuality: qualities[i]})
		if err != nil {
			t.Fatalf("Failed to encode with quality %d: %v", qualities[i], err)
		}
		sizes[i] = len(encodedData)
	}

	// Verify: quality 90 > quality 10, and repeated encodings produce consistent sizes
	if !(sizes[0] > sizes[1] && sizes[0] == sizes[2] && sizes[1] == sizes[3]) {
		t.Errorf("Expected sizes[0] > sizes[1] && sizes[0] == sizes[2] && sizes[1] == sizes[3], got sizes: %v", sizes)
	}
}

func TestJpegEncoderReEncode(t *testing.T) {
	decoder, framebuffer := decodeTestImage(t, "testdata/ferry_sunset.jpg")

	dstBuf := make([]byte, jpegDestinationBufferSize)
	encoder, err := newJpegEncoder(decoder, dstBuf)
	if err != nil {
		t.Fatalf("Failed to create encoder: %v", err)
	}
	defer encoder.Close()

	options := map[int]int{JpegQuality: 90}

	// Encode multiple times with the same encoder
	var previousSize int
	for i := 0; i < 3; i++ {
		encodedData, err := encoder.Encode(framebuffer, options)
		if err != nil {
			t.Fatalf("Encode iteration %d failed: %v", i+1, err)
		}

		if len(encodedData) == 0 {
			t.Fatalf("Encoded data is empty on iteration %d", i+1)
		}

		// Verify consistent output size across iterations
		if i > 0 && len(encodedData) != previousSize {
			t.Errorf("Iteration %d produced %d bytes, iteration %d produced %d bytes (expected consistent size)", i, previousSize, i+1, len(encodedData))
		}

		previousSize = len(encodedData)
	}
}

func TestJpegICCPreservation(t *testing.T) {
	tests := []struct {
		name     string
		filePath string
		wantICC  bool
	}{
		{name: "JPEG with ICC Profile", filePath: "testdata/ferry_sunset.jpg", wantICC: true},
		{name: "JPEG without ICC Profile", filePath: "testdata/ferry_sunset_no_icc.jpg", wantICC: false},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			decoder, framebuffer := decodeTestImage(t, tc.filePath)

			// Get the original ICC profile
			originalICC := decoder.ICC()

			// Encode back to JPEG
			dstBuf := make([]byte, jpegDestinationBufferSize)
			encoder, err := newJpegEncoder(decoder, dstBuf)
			if err != nil {
				t.Fatalf("Failed to create JPEG encoder: %v", err)
			}
			defer encoder.Close()

			options := map[int]int{JpegQuality: 90}
			encodedData, err := encoder.Encode(framebuffer, options)
			if err != nil {
				t.Fatalf("Encode failed unexpectedly: %v", err)
			}
			if len(encodedData) == 0 {
				t.Fatalf("Encoded data is empty")
			}

			// Decode the newly encoded image and check for ICC profile
			decoder2, err := newOpenCVDecoder(encodedData)
			if err != nil {
				t.Fatalf("Failed to create decoder for re-encoded image: %v", err)
			}
			defer decoder2.Close()

			// Read header to initialize decoder
			_, err = decoder2.Header()
			if err != nil {
				t.Fatalf("Failed to get header from re-encoded image: %v", err)
			}

			reEncodedICC := decoder2.ICC()

			verifyICCProfilePreservation(t, originalICC, reEncodedICC, tc.wantICC)
		})
	}
}

func BenchmarkJpegEncoder(b *testing.B) {
	decoder, framebuffer := decodeTestImage(b, "testdata/ferry_sunset_no_icc.jpg")

	qualities := []int{85, 90, 95}

	b.Run("TurboJPEG", func(b *testing.B) {
		for _, quality := range qualities {
			b.Run(fmt.Sprintf("Quality%d", quality), func(b *testing.B) {
				dstBuf := make([]byte, jpegDestinationBufferSize)
				encoder, err := newJpegEncoder(decoder, dstBuf)
				if err != nil {
					b.Fatalf("Failed to create encoder: %v", err)
				}
				defer encoder.Close()

				options := map[int]int{JpegQuality: quality}

				for b.Loop() {
					_, err := encoder.Encode(framebuffer, options)
					if err != nil {
						b.Fatalf("Encode failed: %v", err)
					}
				}
			})
		}
	})

	// Compare against the OpenCV encoder
	b.Run("OpenCV", func(b *testing.B) {
		for _, quality := range qualities {
			b.Run(fmt.Sprintf("Quality%d", quality), func(b *testing.B) {
				dstBuf := make([]byte, jpegDestinationBufferSize)
				options := map[int]int{JpegQuality: quality}

				for b.Loop() {
					encoder, err := newOpenCVEncoder(".jpg", decoder, dstBuf)
					if err != nil {
						b.Fatalf("Failed to create encoder: %v", err)
					}
					_, err = encoder.Encode(framebuffer, options)
					encoder.Close()
					if err != nil {
						b.Fatalf("Encode failed: %v", err)
					}
				}
			})
		}
	})
}
