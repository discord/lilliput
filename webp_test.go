package lilliput

import (
	"os"
	"reflect"
	"testing"
)

const (
	destinationBufferSize = 10 * 1024 * 1024
)

func TestNewWebpDecoder(t *testing.T) {
	var testWebPImage []byte
	var err error
	var decoder *webpDecoder

	testWebPImage, err = os.ReadFile("testdata/tears_of_steel_no_icc.webp")
	if err != nil {
		t.Errorf("Unexpected error while reading webp image: %v", err)
	}
	decoder, err = newWebpDecoder(testWebPImage)
	if err != nil {
		t.Errorf("Unexpected error while decoding webp image data: %v", err)
	}
	decoder.Close()
}

func TestWebpDecoder_Header(t *testing.T) {
	var testWebPImage []byte
	var err error
	var decoder *webpDecoder
	var header *ImageHeader

	if testWebPImage, err = os.ReadFile("testdata/tears_of_steel_no_icc.webp"); err != nil {
		t.Errorf("Unexpected error while reading webp image: %v", err)
	}
	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Errorf("Unexpected error while decoding webp image data: %v", err)
	}
	if header, err = decoder.Header(); err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	if reflect.TypeOf(header).String() != "*lilliput.ImageHeader" {
		t.Errorf("Expected type *lilliput.ImageHeader, got %v", reflect.TypeOf(header))
	}
	decoder.Close()
}

func TestNewWebpEncoder(t *testing.T) {
	var err error
	var testWebPImage []byte
	var decoder *webpDecoder
	var encoder *webpEncoder

	// TEST: no ICC Profile Data
	testWebPImage, err = os.ReadFile("testdata/tears_of_steel_no_icc.webp")
	if err != nil {
		t.Errorf("Unexpected error while reading webp image: %v", err)
	}
	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Errorf("Unexpected error while decoding webp image data: %v", err)
	}
	dstBuf := make([]byte, destinationBufferSize)
	encoder, err = newWebpEncoder(decoder, dstBuf)
	if err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	decoder.Close()
	encoder.Close()

	// TEST: with ICC Profile Data
	if testWebPImage, err = os.ReadFile("testdata/tears_of_steel_icc.webp"); err != nil {
		t.Errorf("Unexpected error while reading webp image: %v", err)
	}
	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Errorf("Unexpected error while decoding webp image data: %v", err)
	}
	dstBuf = make([]byte, destinationBufferSize)
	if encoder, err = newWebpEncoder(decoder, dstBuf); err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	decoder.Close()
	encoder.Close()
}

func TestWebpDecoder_DecodeTo(t *testing.T) {
	var testWebPImage []byte
	var err error
	var decoder *webpDecoder
	var header *ImageHeader
	var framebuffer *Framebuffer

	// TEST: no ICC profile data
	testWebPImage, err = os.ReadFile("testdata/tears_of_steel_no_icc.webp")
	if err != nil {
		t.Fatalf("Failed to read webp image: %v", err)
	}
	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Fatalf("Failed to create a new webp decoder: %v", err)
	}
	defer decoder.Close()
	if header, err = decoder.Header(); err != nil {
		t.Fatalf("Failed to get the header: %v", err)
	}
	framebuffer = NewFramebuffer(header.width, header.height)
	if err = decoder.DecodeTo(framebuffer); err != nil {
		t.Errorf("DecodeTo failed unexpectedly: %v", err)
	}

	// TEST: with ICC profile data
	if testWebPImage, err = os.ReadFile("testdata/tears_of_steel_icc.webp"); err != nil {
		t.Fatalf("Failed to read webp image: %v", err)
	}
	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Fatalf("Failed to create a new webp decoder: %v", err)
	}
	decoder.Close()

	// TEST: an invalid framebuffer
	if err = decoder.DecodeTo(nil); err == nil {
		t.Error("DecodeTo with nil framebuffer should fail, but it did not")
	}
}

func TestWebpEncoder_Encode(t *testing.T) {
	var testWebPImage, dstBuf, encodedData []byte
	var err error
	var decoder *webpDecoder
	var framebuffer *Framebuffer
	var header *ImageHeader
	var options map[int]int
	var encoder *webpEncoder

	// TEST: no ICC profile data
	if testWebPImage, err = os.ReadFile("testdata/tears_of_steel_no_icc.webp"); err != nil {
		t.Fatalf("Failed to read webp image: %v", err)
	}

	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Fatalf("Failed to create a new webp decoder: %v", err)
	}

	if header, err = decoder.Header(); err != nil {
		t.Fatalf("Failed to get the header: %v", err)
	}
	framebuffer = NewFramebuffer(header.width, header.height)
	if err = framebuffer.resizeMat(header.width, header.height, header.pixelType); err != nil {
		t.Fatalf("Failed to resize the framebuffer: %v", err)
	}

	dstBuf = make([]byte, destinationBufferSize)
	if encoder, err = newWebpEncoder(decoder, dstBuf); err != nil {
		t.Fatalf("Failed to create a new webp encoder: %v", err)
	}

	options = map[int]int{}
	if encodedData, err = encoder.Encode(framebuffer, options); err != nil {
		t.Fatalf("Encode failed unexpectedly: %v", err)
	}
	if len(encodedData) == 0 {
		t.Fatalf("Encoded data is empty, but it should not be")
	}
	decoder.Close()
	encoder.Close()

	// TEST: with ICC profile data
	if testWebPImage, err = os.ReadFile("testdata/tears_of_steel_icc.webp"); err != nil {
		t.Fatalf("Failed to read webp image: %v", err)
	}

	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Fatalf("Failed to create a new webp decoder: %v", err)
	}

	dstBuf = make([]byte, destinationBufferSize)
	if encoder, err = newWebpEncoder(decoder, dstBuf); err != nil {
		t.Fatalf("Failed to create a new webp encoder: %v", err)
	}

	header, _ = decoder.Header()
	framebuffer = NewFramebuffer(header.width, header.height)
	options = map[int]int{}
	if err = framebuffer.resizeMat(header.width, header.height, header.pixelType); err != nil {
		t.Fatalf("Failed to resize the framebuffer: %v", err)
	}

	t.Log("Encoding the framebuffer")
	if encodedData, err = encoder.Encode(framebuffer, options); err != nil {
		t.Fatalf("Encode failed unexpectedly: %v", err)
	}
	if len(encodedData) == 0 {
		t.Fatalf("Encoded data is empty, but it should not be")
	}
	decoder.Close()
	encoder.Close()

	// TEST: nil framebuffer
	if testWebPImage, err = os.ReadFile("testdata/tears_of_steel_icc.webp"); err != nil {
		t.Fatalf("Failed to read webp image: %v", err)
	}

	if decoder, err = newWebpDecoder(testWebPImage); err != nil {
		t.Fatalf("Failed to create a new webp decoder: %v", err)
	}

	dstBuf = make([]byte, destinationBufferSize)
	if encoder, err = newWebpEncoder(decoder, dstBuf); err != nil {
		t.Fatalf("Failed to create a new webp encoder: %v", err)
	}

	header, _ = decoder.Header()
	framebuffer = NewFramebuffer(header.width, header.height)
	options = map[int]int{}
	if err = framebuffer.resizeMat(header.width, header.height, header.pixelType); err != nil {
		t.Fatalf("Failed to resize the framebuffer: %v", err)
	}

	if _, err = encoder.Encode(nil, options); err == nil {
		t.Error("Encoding a nil framebuffer should fail, but it did not")
	}
	decoder.Close()
	encoder.Close()
}
