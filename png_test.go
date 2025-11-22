package lilliput

import (
	"errors"
	"fmt"
	"io"
	"testing"
)

const (
	pngDestinationBufferSize = 10 * 1024 * 1024
)

func TestPngEncoderBufferTooSmall(t *testing.T) {
	testCases := []struct {
		name         string
		filePath     string
		bufferSize   int
		options      map[int]int
		expectErrBuf bool
	}{
		{
			name:         "50 bytes",
			filePath:     "testdata/ferry_sunset.png",
			bufferSize:   50,
			options:      map[int]int{PngCompression: 6},
			expectErrBuf: true,
		},
		{
			name:         "1 KiB",
			filePath:     "testdata/ferry_sunset.png",
			bufferSize:   1024,
			options:      map[int]int{PngCompression: 6},
			expectErrBuf: true,
		},
		{
			name:         "10 KiB",
			filePath:     "testdata/ferry_sunset.png",
			bufferSize:   10 * 1024,
			options:      map[int]int{PngCompression: 6},
			expectErrBuf: true,
		},
		{
			name:         "10 MiB",
			filePath:     "testdata/ferry_sunset.png",
			bufferSize:   10 * 1024 * 1024,
			options:      map[int]int{PngCompression: 6},
			expectErrBuf: false,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, framebuffer := decodeTestImage(t, tc.filePath)

			// Create PNG encoder with specific buffer size
			dstBuf := make([]byte, tc.bufferSize)
			encoder, err := newPngEncoder(decoder, dstBuf)
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

func TestPngEncoderInvalidFramebuffer(t *testing.T) {
	t.Run("Nil framebuffer", func(t *testing.T) {
		dstBuf := make([]byte, pngDestinationBufferSize)
		encoder, err := newPngEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{PngCompression: 6}
		_, err = encoder.Encode(nil, options)
		if err != io.EOF {
			t.Errorf("Expected io.EOF with nil framebuffer, got: %v", err)
		}
	})

	t.Run("Zero-dimension framebuffer (0x0)", func(t *testing.T) {
		framebuffer := NewFramebuffer(0, 0)
		defer framebuffer.Close()

		dstBuf := make([]byte, pngDestinationBufferSize)
		encoder, err := newPngEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{PngCompression: 6}
		_, err = encoder.Encode(framebuffer, options)
		if !errors.Is(err, ErrFrameBufNoPixels) {
			t.Errorf("Expected ErrFrameBufNoPixels with 0x0 framebuffer, got: %v", err)
		}
	})

	t.Run("Zero width framebuffer (0x100)", func(t *testing.T) {
		framebuffer := NewFramebuffer(0, 100)
		defer framebuffer.Close()

		dstBuf := make([]byte, pngDestinationBufferSize)
		encoder, err := newPngEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{PngCompression: 6}
		_, err = encoder.Encode(framebuffer, options)
		if !errors.Is(err, ErrFrameBufNoPixels) {
			t.Errorf("Expected ErrFrameBufNoPixels with 0x100 framebuffer, got: %v", err)
		}
	})

	t.Run("Zero height framebuffer (100x0)", func(t *testing.T) {
		framebuffer := NewFramebuffer(100, 0)
		defer framebuffer.Close()

		dstBuf := make([]byte, pngDestinationBufferSize)
		encoder, err := newPngEncoder(nil, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create encoder: %v", err)
		}
		defer encoder.Close()

		options := map[int]int{PngCompression: 6}
		_, err = encoder.Encode(framebuffer, options)
		if !errors.Is(err, ErrFrameBufNoPixels) {
			t.Errorf("Expected ErrFrameBufNoPixels with 100x0 framebuffer, got: %v", err)
		}
	})
}

func TestPngEncoderSmallImages(t *testing.T) {
	testCases := []struct {
		name    string
		options map[int]int
		minSize int // Minimum expected encoded size
	}{
		{
			name:    "Standard compression",
			options: map[int]int{PngCompression: 6},
			minSize: 67, // PNG minimum is 67 bytes
		},
		{
			name:    "Max compression",
			options: map[int]int{PngCompression: 9},
			minSize: 67, // Should still be >= 67 bytes
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			framebuffer := createInitializedFramebuffer(t, 1, 1)

			dstBuf := make([]byte, 10*1024)
			encoder, err := newPngEncoder(nil, dstBuf)
			if err != nil {
				t.Fatalf("Failed to create encoder: %v", err)
			}
			defer encoder.Close()

			encodedData, err := encoder.Encode(framebuffer, tc.options)
			if err != nil {
				t.Fatalf("Encode failed unexpectedly: %v", err)
			}

			if len(encodedData) == 0 {
				t.Errorf("Encoded data is empty")
			}

			if len(encodedData) < tc.minSize {
				t.Errorf("Encoded PNG 1x1 image is %d bytes, expected at least %d bytes", len(encodedData), tc.minSize)
			}

			t.Logf("1x1 pixel .png encoded to %d bytes", len(encodedData))
		})
	}
}

func TestPngEncoderInvalidParameters(t *testing.T) {
	framebuffer := createInitializedFramebuffer(t, 10, 10)

	testCases := []struct {
		name    string
		options map[int]int
	}{
		{
			name:    "Negative compression",
			options: map[int]int{PngCompression: -1},
		},
		{
			name:    "Over 9 compression",
			options: map[int]int{PngCompression: 15},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			dstBuf := make([]byte, pngDestinationBufferSize)
			encoder, err := newPngEncoder(nil, dstBuf)
			if err != nil {
				t.Fatalf("Failed to create encoder: %v", err)
			}
			defer encoder.Close()

			// PNG compression must be in range [0, 9]
			_, err = encoder.Encode(framebuffer, tc.options)
			if !errors.Is(err, ErrInvalidParam) {
				t.Errorf("Expected ErrInvalidParam for out-of-range compression, got: %v", err)
			}
		})
	}
}

func TestPngEncoderReEncode(t *testing.T) {
	decoder, framebuffer := decodeTestImage(t, "testdata/ferry_sunset.png")

	dstBuf := make([]byte, pngDestinationBufferSize)
	encoder, err := newPngEncoder(decoder, dstBuf)
	if err != nil {
		t.Fatalf("Failed to create encoder: %v", err)
	}
	defer encoder.Close()

	options := map[int]int{PngCompression: 6}

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
		t.Logf("Iteration %d: encoded to %d bytes", i+1, len(encodedData))
	}
}

func TestPngICCPreservation(t *testing.T) {
	tests := []struct {
		name     string
		filePath string
		wantICC  bool
	}{
		{name: "PNG with ICC Profile", filePath: "testdata/ferry_sunset.png", wantICC: true},
		{name: "PNG without ICC Profile", filePath: "testdata/ferry_sunset_no_icc.png", wantICC: false},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			decoder, framebuffer := decodeTestImage(t, tc.filePath)

			// Get the original ICC profile
			originalICC := decoder.ICC()

			// Encode back to PNG
			dstBuf := make([]byte, pngDestinationBufferSize)
			encoder, err := newPngEncoder(decoder, dstBuf)
			if err != nil {
				t.Fatalf("Failed to create PNG encoder: %v", err)
			}
			defer encoder.Close()

			options := map[int]int{PngCompression: 6}
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

func BenchmarkPngEncoder(b *testing.B) {
	decoder, framebuffer := decodeTestImage(b, "testdata/ferry_sunset_no_icc.png")

	compressions := []int{3, 6, 9}

	b.Run("libpng", func(b *testing.B) {
		for _, compression := range compressions {
			b.Run(fmt.Sprintf("Compression%d", compression), func(b *testing.B) {
				dstBuf := make([]byte, pngDestinationBufferSize)
				encoder, err := newPngEncoder(decoder, dstBuf)
				if err != nil {
					b.Fatalf("Failed to create encoder: %v", err)
				}
				defer encoder.Close()

				options := map[int]int{PngCompression: compression}

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
		for _, compression := range compressions {
			b.Run(fmt.Sprintf("Compression%d", compression), func(b *testing.B) {
				dstBuf := make([]byte, pngDestinationBufferSize)
				options := map[int]int{PngCompression: compression}

				for b.Loop() {
					encoder, err := newOpenCVEncoder(".png", decoder, dstBuf)
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
