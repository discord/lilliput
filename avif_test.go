package lilliput

import (
	"math"
	"os"
	"reflect"
	"testing"
	"time"
)

// Test Suite Setup
// ----------------------------------------

func TestAvifOperations(t *testing.T) {
	t.Run("Basic Operations", func(t *testing.T) {
		t.Run("NewAvifDecoder", testNewAvifDecoder)
		t.Run("AvifDecoder_Header", testAvifDecoderHeader)
		t.Run("AvifDecoder_Duration", testAvifDecoderDuration)
		t.Run("NewAvifEncoder", testNewAvifEncoder)
		t.Run("AvifDecoder_DecodeTo", testAvifDecoderDecodeTo)
		t.Run("AvifEncoder_Encode", testAvifEncoderEncode)
		t.Run("AvifDecoder_UnknownLoopCount", testAvifDecoderUnknownLoopCount)
	})

	t.Run("Conversion Operations", func(t *testing.T) {
		t.Run("AvifToWebP_Conversion", testAvifToWebPConversion)
		t.Run("NewAvifDecoderWithAnimatedSource", testNewAvifDecoderWithAnimatedSource)
		t.Run("NewAvifEncoderWithWebPAnimatedSource", testNewAvifEncoderWithWebPAnimatedSource)
		t.Run("NewAvifEncoderWithVideoSource", testNewAvifEncoderWithVideoSource)
	})
}

// Basic Decoder Tests
// ----------------------------------------

func testNewAvifDecoder(t *testing.T) {
	testAvifImage, err := os.ReadFile("testdata/colors_sdr_srgb.avif")
	if err != nil {
		t.Fatalf("Unexpected error while reading AVIF image: %v", err)
	}
	decoder, err := newAvifDecoder(testAvifImage, true)
	if err != nil {
		t.Fatalf("Unexpected error while decoding AVIF image data: %v", err)
	}
	defer decoder.Close()
}

func testAvifDecoderHeader(t *testing.T) {
	testAvifImage, err := os.ReadFile("testdata/colors_sdr_srgb.avif")
	if err != nil {
		t.Fatalf("Unexpected error while reading AVIF image: %v", err)
	}
	decoder, err := newAvifDecoder(testAvifImage, true)
	if err != nil {
		t.Fatalf("Unexpected error while decoding AVIF image data: %v", err)
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

func testAvifDecoderDuration(t *testing.T) {
	testCases := []struct {
		name           string
		filename       string
		expectAnimated bool
		wantDuration   float64
	}{
		{
			name:           "Static AVIF",
			filename:       "testdata/colors_sdr_srgb.avif",
			expectAnimated: false,
			wantDuration:   0,
		},
		{
			name:           "Animated AVIF",
			filename:       "testdata/colors-animated-8bpc-alpha-exif-xmp.avif",
			expectAnimated: true,
			wantDuration:   0.833,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testAvifImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Failed to read AVIF image: %v", err)
			}

			decoder, err := newAvifDecoder(testAvifImage, true)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			if tc.expectAnimated {
				duration := decoder.Duration().Seconds()
				if duration <= 0 {
					t.Errorf("Expected animated AVIF with duration > 0, got %v", duration)
				}
				if !almostEqual(duration, tc.wantDuration, 0.01) {
					t.Errorf("Expected duration %v, got %v", tc.wantDuration, duration)
				}
			} else {
				if decoder.Duration().Seconds() != 0 {
					t.Errorf("Expected static AVIF with duration 0, got %v", decoder.Duration().Seconds())
				}
			}
		})
	}
}

func testAvifDecoderUnknownLoopCount(t *testing.T) {
	testAvifImage, err := os.ReadFile("testdata/spinning-globe-unknown-loop-count.avif")
	if err != nil {
		t.Fatalf("Unexpected error while reading AVIF image: %v", err)
	}
	decoder, err := newAvifDecoder(testAvifImage, true)
	if err != nil {
		t.Fatalf("Unexpected error while decoding AVIF image data: %v", err)
	}
	defer decoder.Close()

	// Verify the image is animated
	if !decoder.IsAnimated() {
		t.Error("Expected image to be animated")
	}

	// Check that unknown loop count is treated as infinite (0)
	loopCount := decoder.LoopCount()
	if loopCount != 0 {
		t.Errorf("Expected loop count to be 0 (infinite) for unknown loop count, got %d", loopCount)
	}
}

// Basic Encoder Tests
// ----------------------------------------

func testNewAvifEncoder(t *testing.T) {
	testCases := []struct {
		name     string
		filename string
	}{
		{"No ICC Profile", "testdata/colors_sdr_srgb.avif"},
		{"With ICC Profile", "testdata/paris_icc_exif_xmp.avif"},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testAvifImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Unexpected error while reading AVIF image: %v", err)
			}
			decoder, err := newAvifDecoder(testAvifImage, true)
			if err != nil {
				t.Fatalf("Unexpected error while decoding AVIF image data: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, destinationBufferSize)
			encoder, err := newAvifEncoder(decoder, dstBuf, nil)
			if err != nil {
				t.Fatalf("Unexpected error: %v", err)
			}
			defer encoder.Close()
		})
	}
}

// Decode/Encode Operation Tests
// ----------------------------------------

func testAvifDecoderDecodeTo(t *testing.T) {
	testCases := []struct {
		name     string
		filename string
	}{
		{"No ICC Profile", "testdata/colors_sdr_srgb.avif"},
		{"With ICC Profile", "testdata/paris_icc_exif_xmp.avif"},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testAvifImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Failed to read AVIF image: %v", err)
			}
			decoder, err := newAvifDecoder(testAvifImage, true)
			if err != nil {
				t.Fatalf("Failed to create a new AVIF decoder: %v", err)
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

	t.Run("Invalid Framebuffer", func(t *testing.T) {
		testAvifImage, _ := os.ReadFile("testdata/colors_sdr_srgb.avif")
		decoder, _ := newAvifDecoder(testAvifImage, true)
		defer decoder.Close()

		if err := decoder.DecodeTo(nil); err == nil {
			t.Error("DecodeTo with nil framebuffer should fail, but it did not")
		}
	})
}

func testAvifEncoderEncode(t *testing.T) {
	testCases := []struct {
		name     string
		filename string
		quality  int
	}{
		{"No ICC Profile", "testdata/colors_sdr_srgb.avif", 60},
		{"With ICC Profile", "testdata/paris_icc_exif_xmp.avif", 80},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			testAvifImage, err := os.ReadFile(tc.filename)
			if err != nil {
				t.Fatalf("Failed to read AVIF image: %v", err)
			}

			decoder, err := newAvifDecoder(testAvifImage, true)
			if err != nil {
				t.Fatalf("Failed to create a new AVIF decoder: %v", err)
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
			encoder, err := newAvifEncoder(decoder, dstBuf, nil)
			if err != nil {
				t.Fatalf("Failed to create a new AVIF encoder: %v", err)
			}
			defer encoder.Close()

			options := map[int]int{AvifQuality: tc.quality, AvifSpeed: 10}
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
}

// Conversion Tests
// ----------------------------------------

func testAvifToWebPConversion(t *testing.T) {
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
			name:         "AVIF to WebP conversion with no ICC Profile",
			inputPath:    "testdata/colors_sdr_srgb.avif",
			outputPath:   "testdata/out/colors_sdr_srgb_converted.webp",
			width:        100,
			height:       100,
			quality:      80,
			resizeMethod: ImageOpsFit,
		},
		{
			name:         "AVIF to WebP conversion with ICC Profile",
			inputPath:    "testdata/paris_icc_exif_xmp.avif",
			outputPath:   "testdata/out/paris_icc_exif_xmp_converted.webp",
			width:        200,
			height:       150,
			quality:      60,
			resizeMethod: ImageOpsFit,
		},
		{
			name:         "AVIF to WebP conversion with ICC Profile",
			inputPath:    "testdata/hdr_color_preservation.avif",
			outputPath:   "testdata/out/hdr_color_preservation_converted.webp",
			width:        600,
			height:       550,
			quality:      60,
			resizeMethod: ImageOpsFit,
		},
	}

	runConversionTest(t, testCases, func(tc struct {
		name         string
		inputPath    string
		outputPath   string
		width        int
		height       int
		quality      int
		resizeMethod ImageOpsSizeMethod
	}) (Decoder, error) {
		testAvifImage, err := os.ReadFile(tc.inputPath)
		if err != nil {
			return nil, err
		}
		return newAvifDecoder(testAvifImage, true)
	}, ".webp", WebpQuality)
}

// Animation Tests
// ----------------------------------------

func testNewAvifDecoderWithAnimatedSource(t *testing.T) {
	testCases := []struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		outputType            string
		disableAnimatedOutput bool
	}{
		{
			name:                  "Animated AVIF to WebP encoding",
			inputPath:             "testdata/colors-animated-8bpc-alpha-exif-xmp.avif",
			outputPath:            "testdata/out/animated_sample_out.webp",
			width:                 100,
			height:                100,
			quality:               60,
			resizeMethod:          ImageOpsFit,
			outputType:            ".webp",
			disableAnimatedOutput: false,
		},
		{
			name:                  "Animated AVIF to AVIF encoding",
			inputPath:             "testdata/colors-animated-8bpc-alpha-exif-xmp.avif",
			outputPath:            "testdata/out/animated_sample_out.avif",
			width:                 100,
			height:                100,
			quality:               60,
			resizeMethod:          ImageOpsFit,
			outputType:            ".avif",
			disableAnimatedOutput: false,
		},
	}

	runAnimationTest(t, testCases, func(tc struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		outputType            string
		disableAnimatedOutput bool
	}) (Decoder, error) {
		testAvifImage, err := os.ReadFile(tc.inputPath)
		if err != nil {
			return nil, err
		}
		return newAvifDecoder(testAvifImage, true)
	})
}

func testNewAvifEncoderWithWebPAnimatedSource(t *testing.T) {
	testCases := []struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		outputType            string
		disableAnimatedOutput bool
	}{
		{
			name:                  "Animated WebP to Animated AVIF encoding",
			inputPath:             "testdata/animated-webp-supported.webp",
			outputPath:            "testdata/out/animated-webp-supported_out_fit.avif",
			width:                 400,
			height:                400,
			quality:               95,
			resizeMethod:          ImageOpsFit,
			outputType:            ".avif",
			disableAnimatedOutput: false,
		},
	}

	runAnimationTest(t, testCases, func(tc struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		outputType            string
		disableAnimatedOutput bool
	}) (Decoder, error) {
		testWebpImage, err := os.ReadFile(tc.inputPath)
		if err != nil {
			return nil, err
		}
		return newWebpDecoder(testWebpImage)
	})
}

func testNewAvifEncoderWithVideoSource(t *testing.T) {
	testCases := []struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		outputType            string
		disableAnimatedOutput bool
	}{
		{
			name:                  "MP4 Video to Resized AVIF",
			inputPath:             "testdata/big_buck_bunny_480p_10s_std.mp4",
			outputPath:            "testdata/out/video_resized.avif",
			width:                 320,
			height:                240,
			quality:               60,
			resizeMethod:          ImageOpsFit,
			outputType:            ".avif",
			disableAnimatedOutput: true,
		},
	}

	runAnimationTest(t, testCases, func(tc struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		outputType            string
		disableAnimatedOutput bool
	}) (Decoder, error) {
		testVideoData, err := os.ReadFile(tc.inputPath)
		if err != nil {
			return nil, err
		}
		return newAVCodecDecoder(testVideoData)
	})
}

// Helper Functions
// ----------------------------------------

func runConversionTest(t *testing.T, testCases interface{}, decoderFactory interface{}, outputType string, qualityType int) {
	cases := testCases.([]struct {
		name         string
		inputPath    string
		outputPath   string
		width        int
		height       int
		quality      int
		resizeMethod ImageOpsSizeMethod
	})

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, err := decoderFactory.(func(struct {
				name         string
				inputPath    string
				outputPath   string
				width        int
				height       int
				quality      int
				resizeMethod ImageOpsSizeMethod
			}) (Decoder, error))(tc)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, destinationBufferSize)
			options := &ImageOptions{
				FileType:             outputType,
				NormalizeOrientation: true,
				EncodeOptions:        map[int]int{qualityType: tc.quality},
				ResizeMethod:         tc.resizeMethod,
				Width:                tc.width,
				Height:               tc.height,
				EncodeTimeout:        time.Second * 30,
			}

			ops := NewImageOps(20000)
			defer ops.Close()

			newDst, err := ops.Transform(decoder, options, dstBuf)
			if err != nil {
				t.Fatalf("Transform failed: %v", err)
			}

			if len(newDst) == 0 {
				t.Fatal("Transform returned empty data")
			}

			ensureOutputDir(t)
			if err = os.WriteFile(tc.outputPath, newDst, 0644); err != nil {
				t.Fatalf("Failed to write output file: %v", err)
			}
		})
	}
}

func runAnimationTest(t *testing.T, testCases interface{}, decoderFactory interface{}) {
	cases := testCases.([]struct {
		name                  string
		inputPath             string
		outputPath            string
		width                 int
		height                int
		quality               int
		resizeMethod          ImageOpsSizeMethod
		outputType            string
		disableAnimatedOutput bool
	})

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			decoder, err := decoderFactory.(func(struct {
				name                  string
				inputPath             string
				outputPath            string
				width                 int
				height                int
				quality               int
				resizeMethod          ImageOpsSizeMethod
				outputType            string
				disableAnimatedOutput bool
			}) (Decoder, error))(tc)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}
			defer decoder.Close()

			dstBuf := make([]byte, destinationBufferSize)
			options := &ImageOptions{
				FileType:              tc.outputType,
				NormalizeOrientation:  true,
				EncodeOptions:         map[int]int{AvifQuality: tc.quality, AvifSpeed: 10},
				ResizeMethod:          tc.resizeMethod,
				Width:                 tc.width,
				Height:                tc.height,
				EncodeTimeout:         time.Second * 30,
				DisableAnimatedOutput: tc.disableAnimatedOutput,
			}

			ops := NewImageOps(2000)
			defer ops.Close()

			newDst, err := ops.Transform(decoder, options, dstBuf)
			if err != nil {
				t.Fatalf("Transform failed: %v", err)
			}

			if len(newDst) == 0 {
				t.Fatal("Transform returned empty data")
			}

			ensureOutputDir(t)
			if err = os.WriteFile(tc.outputPath, newDst, 0644); err != nil {
				t.Fatalf("Failed to write output file: %v", err)
			}
		})
	}
}

func ensureOutputDir(t *testing.T) {
	if _, err := os.Stat("testdata/out"); os.IsNotExist(err) {
		if err = os.Mkdir("testdata/out", 0755); err != nil {
			t.Fatalf("Failed to create output directory: %v", err)
		}
	}
}

func almostEqual(a, b, tolerance float64) bool {
	return math.Abs(a-b) <= tolerance
}
