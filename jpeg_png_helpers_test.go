package lilliput

import (
	"bytes"
	"os"
	"testing"
)

// This file contains test helpers only, and no tests.

// decodeTestImage loads and decodes a test image file, returning the decoder and framebuffer.
// Both resources are automatically closed via t.Cleanup, so the caller should not close them.
func decodeTestImage(t testing.TB, filePath string) (Decoder, *Framebuffer) {
	t.Helper()
	imgData, err := os.ReadFile(filePath)
	if err != nil {
		t.Fatalf("Failed to read image file: %v", err)
	}

	decoder, err := newOpenCVDecoder(imgData)
	if err != nil {
		t.Fatalf("Failed to create decoder: %v", err)
	}
	t.Cleanup(func() {
		decoder.Close()
	})

	header, err := decoder.Header()
	if err != nil {
		t.Fatalf("Failed to get header: %v", err)
	}

	framebuffer := NewFramebuffer(header.width, header.height)
	t.Cleanup(func() {
		framebuffer.Close()
	})
	if err = decoder.DecodeTo(framebuffer); err != nil {
		t.Fatalf("DecodeTo failed: %v", err)
	}

	return decoder, framebuffer
}

// createInitializedFramebuffer creates and initializes a framebuffer with 3 channels (RGB).
// The framebuffer is automatically closed via t.Cleanup, so the caller should not close it.
func createInitializedFramebuffer(t testing.TB, width, height int) *Framebuffer {
	t.Helper()
	framebuffer := NewFramebuffer(width, height)
	t.Cleanup(func() {
		framebuffer.Close()
	})
	if err := framebuffer.Create3Channel(width, height); err != nil {
		t.Fatalf("Failed to initialize framebuffer: %v", err)
	}
	return framebuffer
}

// verifyICCProfilePreservation checks that ICC profiles are correctly preserved or absent
// in re-encoded images based on the wantICC flag.
func verifyICCProfilePreservation(t testing.TB, originalICC, reEncodedICC []byte, wantICC bool) {
	t.Helper()
	if wantICC {
		if len(originalICC) == 0 {
			t.Fatalf("Original image should have ICC profile but doesn't")
		}
		if len(reEncodedICC) == 0 {
			t.Errorf("ICC profile was not preserved when encoding (expected %d bytes, got 0)", len(originalICC))
		} else if !bytes.Equal(originalICC, reEncodedICC) {
			t.Errorf("ICC profile was changed during encoding (original: %d bytes, new: %d bytes)", len(originalICC), len(reEncodedICC))
		}
	} else {
		if len(reEncodedICC) > 0 {
			t.Errorf("Re-encoded image should not have ICC profile but has %d bytes", len(reEncodedICC))
		}
	}
}
