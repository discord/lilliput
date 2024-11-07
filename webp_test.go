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

func TestWebPOperations(t *testing.T) {
	t.Run("NewWebpDecoder", testNewWebpDecoder)
	t.Run("WebpDecoder_Header", testWebpDecoderHeader)
	t.Run("NewWebpEncoder", testNewWebpEncoder)
	t.Run("WebpDecoder_DecodeTo", testWebpDecoderDecodeTo)
	t.Run("WebpEncoder_Encode", testWebpEncoderEncode)
	t.Run("NewWebpEncoderWithAnimatedWebPSource", testNewWebpEncoderWithAnimatedWebPSource)
	t.Run("NewWebpEncoderWithAnimatedGIFSource", testNewWebpEncoderWithAnimatedGIFSource)
}

func testNewWebpDecoder(t *testing.T) {
	testWebPImage, err := os.ReadFile("testdata/tears_of_steel_no_icc.webp")
	if err != nil {
		t.Fatalf("Unexpected error while reading webp image: %v", err)
	}
	decoder, err := newWebpDecoder(testWebPImage)
	if err != nil {
		t.Fatalf("Unexpected error while decoding webp image data: %v", err)
	}
	defer decoder.Close()
}

func testWebpDecoderHeader(t *testing.T) {
	testWebPImage, err := os.ReadFile("testdata/tears_of_steel_no_icc.webp")
	if err != nil {
		t.Fatalf("Unexpected error while reading webp image: %v", err)
	}
	decoder, err := newWebpDecoder(testWebPImage)
	if err != nil {
		t.Fatalf("Unexpected error while decoding webp image data: %v", err)
	}
	defer decoder.Close()

	header, err := decoder.Header()
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}
	if reflect.TypeOf(header).String() != "*lilliput.ImageHeader" {
		t.Fatalf("Expected type *lilliput.ImageHeader, got %v", reflect.TypeOf(header))
	}
}

func testNewWebpEncoder(t *testing.T) {
	testCases := []struct {
		name     string
		filename string
	}{
		{"No ICC Profile", "testdata/tears_of_steel_no_icc.webp"},
		{"With ICC Profile", "testdata/tears_of_steel_icc.webp"},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testWebPImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Unexpected error while reading webp image: %v", err)
			}
			decoder, err := newWebpDecoder(testWebPImage)
			if err != nil {
				t.Fatalf("Unexpected error while decoding webp image data: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, destinationBufferSize)
			encoder, err := newWebpEncoder(decoder, dstBuf)
			if err != nil {
				t.Fatalf("Unexpected error: %v", err)
			}
			defer encoder.Close()
		})
	}
}

func testWebpDecoderDecodeTo(t *testing.T) {
	testCases := []struct {
		name     string
		filename string
	}{
		{"No ICC Profile", "testdata/tears_of_steel_no_icc.webp"},
		{"With ICC Profile", "testdata/tears_of_steel_icc.webp"},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testWebPImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Failed to read webp image: %v", err)
			}
			decoder, err := newWebpDecoder(testWebPImage)
			if err != nil {
				t.Fatalf("Failed to create a new webp decoder: %v", err)
			}
			defer decoder.Close()

			header, err := decoder.Header()
			if err != nil {
				t.Fatalf("Failed to get the header: %v", err)
			}
			framebuffer := NewFramebuffer(header.width, header.height)
			if err = decoder.DecodeTo(framebuffer); err != nil {
				t.Errorf("DecodeTo failed unexpectedly: %v", err)
			}
		})
	}

	// Test invalid framebuffer
	t.Run("Invalid Framebuffer", func(t *testing.T) {
		testWebPImage, _ := os.ReadFile("testdata/tears_of_steel_no_icc.webp")
		decoder, _ := newWebpDecoder(testWebPImage)
		defer decoder.Close()

		if err := decoder.DecodeTo(nil); err == nil {
			t.Error("DecodeTo with nil framebuffer should fail, but it did not")
		}
	})
}

func testWebpEncoderEncode(t *testing.T) {
	testCases := []struct {
		name     string
		filename string
		quality  int
	}{
		{"No ICC Profile", "testdata/tears_of_steel_no_icc.webp", 60},
		{"With ICC Profile", "testdata/tears_of_steel_icc.webp", 80},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testWebPImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Failed to read webp image: %v", err)
			}

			decoder, err := newWebpDecoder(testWebPImage)
			if err != nil {
				t.Fatalf("Failed to create a new webp decoder: %v", err)
			}
			defer decoder.Close()

			header, err := decoder.Header()
			if err != nil {
				t.Fatalf("Failed to get the header: %v", err)
			}
			framebuffer := NewFramebuffer(header.width, header.height)
			if err = framebuffer.resizeMat(header.width, header.height, header.pixelType); err != nil {
				t.Fatalf("Failed to resize the framebuffer: %v", err)
			}

			dstBuf := make([]byte, destinationBufferSize)
			encoder, err := newWebpEncoder(decoder, dstBuf)
			if err != nil {
				t.Fatalf("Failed to create a new webp encoder: %v", err)
			}
			defer encoder.Close()

			options := map[int]int{WebpQuality: tc.quality}
			encodedData, err := encoder.Encode(framebuffer, options)
			if err != nil {
				t.Fatalf("Encode failed unexpectedly: %v", err)
			}
			if encodedData, err = encoder.Encode(nil, options); err != nil {
				t.Fatalf("Encode of empty frame failed unexpectedly: %v", err)
			}
			if len(encodedData) == 0 {
				t.Fatalf("Encoded data is empty, but it should not be")
			}
		})
	}

	// Test nil framebuffer
	t.Run("Nil Framebuffer", func(t *testing.T) {
		testWebPImage, err := os.ReadFile("testdata/tears_of_steel_icc.webp")
		if err != nil {
			t.Fatalf("Failed to read webp image: %v", err)
		}

		decoder, err := newWebpDecoder(testWebPImage)
		if err != nil {
			t.Fatalf("Failed to create a new webp decoder: %v", err)
		}
		defer decoder.Close()

		dstBuf := make([]byte, destinationBufferSize)
		encoder, err := newWebpEncoder(decoder, dstBuf)
		if err != nil {
			t.Fatalf("Failed to create a new webp encoder: %v", err)
		}
		defer encoder.Close()

		header, _ := decoder.Header()
		framebuffer := NewFramebuffer(header.width, header.height)
		options := map[int]int{}
		if err = framebuffer.resizeMat(header.width, header.height, header.pixelType); err != nil {
			t.Fatalf("Failed to resize the framebuffer: %v", err)
		}

		if _, err = encoder.Encode(nil, options); err == nil {
			t.Error("Encoding a nil framebuffer should fail, but it did not")
		}
	})
}

func testNewWebpEncoderWithAnimatedWebPSource(t *testing.T) {
	testCases := []struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		disableAnimatedOutput bool
	}{
		{
			name:         "Animated WebP - Party Discord",
			inputPath:    "testdata/party-discord.webp",
			outputPath:   "testdata/out/party-discord_out_webpsource_resize.webp",
			width:        26,
			height:       17,
			quality:      60,
			resizeMethod: ImageOpsResize,
		},
		{
			name:         "Animated WebP - Resize #1",
			inputPath:    "testdata/ferry_sunset.webp",
			outputPath:   "testdata/out/ferry_sunset_out_resize.webp",
			width:        266,
			height:       99,
			quality:      60,
			resizeMethod: ImageOpsResize,
		},
		{
			name:         "Animated WebP - Resize #2",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out_resize.webp",
			width:        400,
			height:       400,
			quality:      60,
			resizeMethod: ImageOpsResize,
		},
		{
			name:         "Animated WebP - Fit #1",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out_fit.webp",
			width:        400,
			height:       400,
			quality:      80,
			resizeMethod: ImageOpsFit,
		},
		{
			name:         "Animated WebP - No resize",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out_no_resize.webp",
			width:        0,
			height:       0,
			quality:      100,
			resizeMethod: ImageOpsNoResize,
		},
		{
			name:         "Animated WebP - Fit #2",
			inputPath:    "testdata/animated-webp-supported.webp",
			outputPath:   "testdata/out/animated-webp-supported_out.webp",
			width:        200,
			height:       200,
			quality:      60,
			resizeMethod: ImageOpsFit,
		},
		{
			name:                  "Animated WebP - create single frame",
			inputPath:             "testdata/big_buck_bunny_720_5s.webp",
			outputPath:            "testdata/out/big_buck_bunny_720_5s_single_frame_out.webp",
			width:                 200,
			height:                200,
			quality:               60,
			resizeMethod:          ImageOpsFit,
			disableAnimatedOutput: true,
		},
		{
			name:         "Animated WebP - Crashing input",
			inputPath:    "testdata/8202024-BGS-Headless-Horseman-OO-1200x1200-optimize.webp",
			outputPath:   "testdata/out/8202024-BGS-Headless-Horseman-OO-1200x1200-optimize_out.webp",
			width:        200,
			height:       200,
			quality:      60,
			resizeMethod: ImageOpsFit,
		},
		{
			name:         "Animated WebP - complex dispose and blend",
			inputPath:    "testdata/complex_dispose_and_blend.webp",
			outputPath:   "testdata/out/complex_dispose_and_blend_out.webp",
			width:        960,
			height:       540,
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
				FileType:              ".webp",
				NormalizeOrientation:  true,
				EncodeOptions:         map[int]int{WebpQuality: tc.quality},
				ResizeMethod:          tc.resizeMethod,
				Width:                 tc.width,
				Height:                tc.height,
				EncodeTimeout:         time.Second * 300,
				DisableAnimatedOutput: tc.disableAnimatedOutput,
			}

			ops := NewImageOps(2000)
			var newDst []byte
			newDst, err = ops.Transform(decoder, options, dstBuf)
			if err != nil {
				decoder.Close()
				ops.Close()
				t.Errorf("Transform() error for %s: %v", tc.inputPath, err)
				return
			}

			// verify length of newDst
			if len(newDst) == 0 {
				decoder.Close()
				ops.Close()
				t.Errorf("Transform() returned empty data for %s", tc.inputPath)
			}

			// write the new WebP image to disk
			if tc.outputPath != "" {
				// create output directory if it does not exist
				if _, err := os.Stat("testdata/out"); os.IsNotExist(err) {
					if err = os.Mkdir("testdata/out", 0755); err != nil {
						decoder.Close()
						ops.Close()
						t.Errorf("Failed to create output directory: %v", err)
						return
					}
				}

				if err = os.WriteFile(tc.outputPath, newDst, 0644); err != nil {
					decoder.Close()
					ops.Close()
					t.Errorf("Failed to write %s: %v", tc.outputPath, err)
				}
			}
			decoder.Close()
			ops.Close()
		})
	}
}

func testNewWebpEncoderWithAnimatedGIFSource(t *testing.T) {
	testCases := []struct {
		name         string
		inputPath    string
		outputPath   string
		width        int
		height       int
		quality      int
		resizeMethod ImageOpsSizeMethod
		wantLoops    int
	}{
		{
			name:         "Animated GIF with alpha channel",
			inputPath:    "testdata/party-discord.gif",
			outputPath:   "testdata/out/party-discord_out_resize.webp",
			width:        27,
			height:       17,
			quality:      80,
			resizeMethod: ImageOpsResize,
			wantLoops:    0,
		},
		{
			name:         "Animated GIF with specific loop count",
			inputPath:    "testdata/no-loop.gif",
			outputPath:   "testdata/out/no-loop_out.webp",
			width:        200,
			height:       200,
			quality:      80,
			resizeMethod: ImageOpsResize,
			wantLoops:    1,
		},
		{
			name:         "Animated GIF with duplicate number of loop count, use the first loop count",
			inputPath:    "testdata/duplicate_number_of_loops.gif",
			outputPath:   "testdata/out/duplicate_number_of_loops.webp",
			width:        200,
			height:       200,
			quality:      80,
			resizeMethod: ImageOpsResize,
			wantLoops:    2,
		},
		{
			name:         "Animated GIF with multiple extension blocks",
			inputPath:    "testdata/dispose_bgnd.gif",
			outputPath:   "testdata/out/dispose_bgnd.webp",
			width:        200,
			height:       200,
			quality:      80,
			resizeMethod: ImageOpsResize,
			wantLoops:    0,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			var err error
			var testWebPImage []byte
			var decoder *gifDecoder

			// Read the input GIF file
			testWebPImage, err = os.ReadFile(tc.inputPath)
			if err != nil {
				t.Errorf("Unexpected error while reading %s: %v", tc.inputPath, err)
				return
			}

			// Decode the GIF image
			if decoder, err = newGifDecoder(testWebPImage); err != nil {
				t.Errorf("Unexpected error while decoding %s: %v", tc.inputPath, err)
				return
			}

			// Verify loop count
			if decoder.LoopCount() != tc.wantLoops {
				t.Errorf("Loop count = %d, want %d", decoder.LoopCount(), tc.wantLoops)
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

			ops := NewImageOps(2000)
			var newDst []byte
			newDst, err = ops.Transform(decoder, options, dstBuf)
			if err != nil {
				decoder.Close()
				ops.Close()
				t.Errorf("Transform() error for %s: %v", tc.inputPath, err)
				return
			}

			// verify length of newDst
			if len(newDst) == 0 {
				decoder.Close()
				ops.Close()
				t.Errorf("Transform() returned empty data for %s", tc.inputPath)
			}

			// write the new WebP image to disk
			if tc.outputPath != "" {
				// create output directory if it does not exist
				if _, err := os.Stat("testdata/out"); os.IsNotExist(err) {
					if err = os.Mkdir("testdata/out", 0755); err != nil {
						decoder.Close()
						ops.Close()
						t.Errorf("Failed to create output directory: %v", err)
						return
					}
				}

				if err = os.WriteFile(tc.outputPath, newDst, 0644); err != nil {
					decoder.Close()
					ops.Close()
					t.Errorf("Failed to write %s: %v", tc.outputPath, err)
				}
			}
			decoder.Close()
			ops.Close()
		})
	}
}
