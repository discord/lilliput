package lilliput

import (
	"errors"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"testing"
	"time"
)

type encoderConfig struct {
	method         int // 0-6, where 0 is fastest and 6 is best compression
	quality        int // 0-100
	filterStrength int // 0-100
	filterType     int // 0-1
	autofilter     int // 0-1
	partitions     int // 0-3
	segments       int // 1-4
	preprocessing  int // 0-1
	threads        int // 1-n
	palette        int // 0-1
}

func (c encoderConfig) String() string {
	return fmt.Sprintf("m%d_q%d_fs%d_ft%d_af%d_p%d_s%d_pp%d_t%d_pl%d",
		c.method, c.quality, c.filterStrength, c.filterType,
		c.autofilter, c.partitions, c.segments, c.preprocessing, c.threads, c.palette)
}

type benchmarkResult struct {
	config     encoderConfig
	duration   time.Duration
	outputSize int64
	psnr       float64
}

var testConfigs = []encoderConfig{
	// Default settings
	{method: 4, quality: 80, filterStrength: 60, filterType: 1, autofilter: 0, partitions: 0, segments: 4, preprocessing: 0, threads: 0, palette: 0},

	// Ultra fast encoding - WebP optimized
	{method: 0, quality: 60, filterStrength: 20, filterType: 0, autofilter: 0, partitions: 0, segments: 1, preprocessing: 0, threads: 1, palette: 0},
	{method: 1, quality: 60, filterStrength: 20, filterType: 0, autofilter: 0, partitions: 0, segments: 1, preprocessing: 0, threads: 1, palette: 0},

	// Ultra fast encoding - GIF optimized (with palette)
	{method: 0, quality: 60, filterStrength: 20, filterType: 0, autofilter: 0, partitions: 0, segments: 1, preprocessing: 0, threads: 1, palette: 1},

	// Fast encoding, lower quality
	{method: 0, quality: 75, filterStrength: 20, filterType: 0, autofilter: 0, partitions: 0, segments: 1, preprocessing: 0, threads: 1, palette: 0},
	{method: 1, quality: 75, filterStrength: 20, filterType: 0, autofilter: 0, partitions: 0, segments: 1, preprocessing: 0, threads: 1, palette: 0},

	// Fast encoding - GIF optimized (with palette)
	{method: 0, quality: 75, filterStrength: 20, filterType: 0, autofilter: 0, partitions: 0, segments: 1, preprocessing: 0, threads: 1, palette: 1},

	// Intermediate method (2)
	{method: 2, quality: 75, filterStrength: 30, filterType: 0, autofilter: 0, partitions: 0, segments: 1, preprocessing: 0, threads: 1, palette: 0},

	// Balanced encoding
	{method: 3, quality: 80, filterStrength: 40, filterType: 1, autofilter: 1, partitions: 1, segments: 2, preprocessing: 1, threads: 1, palette: 0},
	{method: 4, quality: 80, filterStrength: 40, filterType: 1, autofilter: 1, partitions: 1, segments: 2, preprocessing: 1, threads: 1, palette: 0},

	// High quality encoding
	{method: 5, quality: 90, filterStrength: 60, filterType: 1, autofilter: 1, partitions: 2, segments: 3, preprocessing: 1, threads: 1, palette: 0},
	{method: 6, quality: 90, filterStrength: 60, filterType: 1, autofilter: 1, partitions: 2, segments: 3, preprocessing: 1, threads: 1, palette: 0},

	// Segment count variations (using method 4 as baseline)
	{method: 4, quality: 80, filterStrength: 40, filterType: 1, autofilter: 1, partitions: 1, segments: 1, preprocessing: 1, threads: 1, palette: 0},
	{method: 4, quality: 80, filterStrength: 40, filterType: 1, autofilter: 1, partitions: 1, segments: 3, preprocessing: 1, threads: 1, palette: 0},
	{method: 4, quality: 80, filterStrength: 40, filterType: 1, autofilter: 1, partitions: 1, segments: 4, preprocessing: 1, threads: 1, palette: 0},
}

// calculatePSNR calculates the Peak Signal-to-Noise Ratio between original and transformed first frames
func calculatePSNR(origBuffer, transBuffer *Framebuffer) (float64, error) {
	if origBuffer == nil || transBuffer == nil {
		return 0, errors.New("nil buffer")
	}

	if len(origBuffer.buf) != len(transBuffer.buf) {
		return 0, fmt.Errorf("buffer size mismatch: %d != %d", len(origBuffer.buf), len(transBuffer.buf))
	}

	var mse float64
	n := len(origBuffer.buf)

	// Calculate MSE across all color channels
	for i := 0; i < n; i++ {
		diff := float64(origBuffer.buf[i]) - float64(transBuffer.buf[i])
		mse += diff * diff
	}

	mse /= float64(n)

	if mse == 0 {
		return math.Inf(1), nil // Perfect match
	}

	// Calculate PSNR using max pixel value of 255 for 8-bit images
	psnr := 20*math.Log10(255) - 10*math.Log10(mse)
	return psnr, nil
}

func BenchmarkWebPEncoding(b *testing.B) {
	testCases := []struct {
		name      string
		inputPath string
	}{
		{"AnimatedWebP", "testdata/animated-webp-supported.webp"},
		{"AnimatedGIF", "testdata/big_buck_bunny_720_5s.gif"},
	}

	for _, tc := range testCases {
		b.Run(tc.name, func(b *testing.B) {
			// Read input file
			inputData, err := os.ReadFile(tc.inputPath)
			if err != nil {
				b.Fatalf("Failed to read input file: %v", err)
			}

			// Create output directory if it doesn't exist
			outDir := filepath.Join("testdata", "benchmark_out")
			if err := os.MkdirAll(outDir, 0755); err != nil {
				b.Fatalf("Failed to create output directory: %v", err)
			}

			for _, config := range testConfigs {
				b.Run(config.String(), func(b *testing.B) {
					var decoder Decoder
					var err error

					// Create appropriate decoder based on input type
					if filepath.Ext(tc.inputPath) == ".gif" {
						decoder, err = newGifDecoder(inputData)
					} else {
						decoder, err = newWebpDecoder(inputData)
					}
					if err != nil {
						b.Fatalf("Failed to create decoder: %v", err)
					}
					// defer decoder.Close()

					// Get original dimensions and first frame for PSNR calculation
					header, err := decoder.Header()
					if err != nil {
						b.Fatalf("Failed to get header: %v", err)
					}

					// Create framebuffer for original image
					origBuffer := NewFramebuffer(header.Width(), header.Height())
					defer origBuffer.Close()

					// Create framebuffer for transformed image
					transBuffer := NewFramebuffer(header.Width(), header.Height())
					defer transBuffer.Close()

					// Initialize framebuffers based on pixel type
					if header.PixelType().Channels() == 4 {
						if err := origBuffer.Create4Channel(header.Width(), header.Height()); err != nil {
							b.Fatalf("Failed to create original buffer: %v", err)
						}
						if err := transBuffer.Create4Channel(header.Width(), header.Height()); err != nil {
							b.Fatalf("Failed to create transformed buffer: %v", err)
						}
					} else {
						if err := origBuffer.Create3Channel(header.Width(), header.Height()); err != nil {
							b.Fatalf("Failed to create original buffer: %v", err)
						}
						if err := transBuffer.Create3Channel(header.Width(), header.Height()); err != nil {
							b.Fatalf("Failed to create transformed buffer: %v", err)
						}
					}

					// Decode first frame of original image
					if err := decoder.DecodeTo(origBuffer); err != nil {
						b.Fatalf("Failed to decode original frame: %v", err)
					}

					options := &ImageOptions{
						FileType:             ".webp",
						NormalizeOrientation: true,
						Width:                header.Width(),
						Height:               header.Height(),
						ResizeMethod:         ImageOpsNoResize,
						EncodeTimeout:        time.Second * 300,
						EncodeOptions: map[int]int{
							WebpQuality:        config.quality,
							WebpMethod:         config.method,
							WebpFilterStrength: config.filterStrength,
							WebpFilterType:     config.filterType,
							WebpAutofilter:     config.autofilter,
							WebpPartitions:     config.partitions,
							WebpSegments:       config.segments,
							WebpPreprocessing:  config.preprocessing,
							WebpThreadLevel:    config.threads,
							WebpPalette:        config.palette,
						},
					}

					dstBuf := make([]byte, destinationBufferSize)
					ops := NewImageOps(8192)
					defer ops.Close()

					b.ResetTimer()
					var lastOutput []byte

					for i := 0; i < b.N; i++ {
						output, err := ops.Transform(decoder, options, dstBuf)
						if err != nil {
							b.Fatalf("Transform failed: %v", err)
						}
						lastOutput = output

						// Reset decoder for next iteration
						decoder.Close()
						if filepath.Ext(tc.inputPath) == ".gif" {
							decoder, _ = newGifDecoder(inputData)
						} else {
							decoder, _ = newWebpDecoder(inputData)
						}
					}

					b.StopTimer()

					// Save the last output for analysis
					outPath := filepath.Join(outDir, fmt.Sprintf("%s_%s.webp",
						filepath.Base(tc.inputPath), config.String()))
					if err := os.WriteFile(outPath, lastOutput, 0644); err != nil {
						b.Fatalf("Failed to write output file: %v", err)
					}

					// Record results
					fileInfo, err := os.Stat(outPath)
					if err != nil {
						b.Fatalf("Failed to get output file info: %v", err)
					}

					// Calculate PSNR using the last output
					transformedDecoder, err := newWebpDecoder(lastOutput)
					if err != nil {
						b.Fatalf("Failed to create decoder for transformed image: %v", err)
					}
					defer transformedDecoder.Close()

					// Decode first frame of transformed image
					if err := transformedDecoder.DecodeTo(transBuffer); err != nil {
						b.Fatalf("Failed to decode transformed frame: %v", err)
					}

					psnr, err := calculatePSNR(origBuffer, transBuffer)
					if err != nil {
						b.Fatalf("Failed to calculate PSNR: %v", err)
					}

					result := benchmarkResult{
						config:     config,
						duration:   b.Elapsed() / time.Duration(b.N),
						outputSize: fileInfo.Size(),
						psnr:       psnr,
					}

					b.ReportMetric(float64(result.duration.Milliseconds()), "ms/op")
					b.ReportMetric(float64(result.outputSize), "output_size_bytes/op")
					b.ReportMetric(result.psnr, "psnr_db/op")
				})
			}
		})
	}
}
