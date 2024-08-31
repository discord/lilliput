package lilliput

import (
	"os"
	"reflect"
	"testing"
	"time"
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
	if encodedData, err = encoder.Encode(nil, options); err != nil {
		t.Fatalf("Encode of empty frame failed unexpectedly: %v", err)
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
	if encodedData, err = encoder.Encode(nil, options); err != nil {
		t.Fatalf("Encode of empty frame failed unexpectedly: %v", err)
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

func TestNewWebpEncoderWithAnimatedWebPSource(t *testing.T) {
	testCases := []struct {
		name         string
		inputPath    string
		outputPath   string
		width        int
		height       int
		quality      int
		resizeMethod ImageOpsSizeMethod
	}{
		{
			name:         "Animated WebP - Supported",
			inputPath:    "testdata/ferry_sunset.webp",
			outputPath:   "testdata/out/ferry_sunset_out_resize.webp",
			width:        266,
			height:       99,
			quality:      60,
			resizeMethod: ImageOpsResize,
		},
		{
			name:         "Animated WebP - Supported",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out_resize.webp",
			width:        400,
			height:       400,
			quality:      60,
			resizeMethod: ImageOpsResize,
		},
		{
			name:         "Animated WebP - Supported",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out_fit.webp",
			width:        400,
			height:       400,
			quality:      80,
			resizeMethod: ImageOpsFit,
		},
		{
			name:         "Animated WebP - Supported",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out_no_resize.webp",
			width:        0,
			height:       0,
			quality:      100,
			resizeMethod: ImageOpsNoResize,
		},
		{
			name:         "Animated WebP - Supported",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out.webp",
			width:        200,
			height:       200,
			quality:      60,
			resizeMethod: ImageOpsFit,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			var err error
			var testWebPImage []byte
			var decoder *webpDecoder

			// Read the input WebP file
			testWebPImage, err = os.ReadFile(tc.inputPath)
			if err != nil {
				t.Errorf("Unexpected error while reading %s: %v", tc.inputPath, err)
				return
			}

			// Decode the WebP image
			if decoder, err = newWebpDecoder(testWebPImage); err != nil {
				t.Errorf("Unexpected error while decoding %s: %v", tc.inputPath, err)
				return
			}

			dstBuf := make([]byte, destinationBufferSize)

			options := &ImageOptions{
				FileType:             ".webp",
				NormalizeOrientation: true,
				EncodeOptions:        map[int]int{WebpQuality: tc.quality},
				ResizeMethod:         tc.resizeMethod,
				Width:                tc.width,
				Height:               tc.height,
				EncodeTimeout:        time.Second * 300,
			}

			ops := NewImageOps(50000)
			var newDst []byte
			newDst, err = ops.Transform(decoder, options, dstBuf)
			if err != nil {
				decoder.Close()
				t.Errorf("Transform() error for %s: %v", tc.inputPath, err)
				return
			}

			// verify length of newDst
			if len(newDst) == 0 {
				decoder.Close()
				t.Errorf("Transform() returned empty data for %s", tc.inputPath)
			}

			// write the new WebP image to disk
			if tc.outputPath != "" {
				// create output directory if it does not exist
				if _, err := os.Stat("testdata/out"); os.IsNotExist(err) {
					if err = os.Mkdir("testdata/out", 0755); err != nil {
						decoder.Close()
						t.Errorf("Failed to create output directory: %v", err)
						return
					}
				}

				if err = os.WriteFile(tc.outputPath, newDst, 0644); err != nil {
					decoder.Close()
					t.Errorf("Failed to write %s: %v", tc.outputPath, err)
				}
			}
			decoder.Close()
		})
	}
}
