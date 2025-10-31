package lilliput

// #include "avcodec.hpp"
import "C"

import (
	"io"
	"time"
	"unsafe"
)

const probeBytesLimit = 32 * 1024
const atomHeaderSize = 8

// Set HEVC decoder enablement behind a build flag, defaults to off
// Enable by building/running with "-ldflags=-X=github.com/discord/lilliput.hevcEnabled=true"
var hevcEnabled string

// Set AV1 decoder enablement behind a build flag, defaults to off
// Enable by building/running with "-ldflags=-X=github.com/discord/lilliput.av1Enabled=true"
var av1Enabled string

// avCodecDecoder handles decoding of various video/image formats using FFmpeg's avcodec.
type avCodecDecoder struct {
	decoder             C.avcodec_decoder
	mat                 C.opencv_mat
	buf                 []byte
	hasDecoded          bool
	maybeMP4            bool
	isStreamable        bool
	hasSubtitles        bool
	multiFrameMode      bool
	frameSampleInterval float64
}

// newAVCodecDecoder creates a new decoder instance from the provided buffer.
// Returns an error if the buffer is too small or contains invalid data.
func newAVCodecDecoder(buf []byte) (*avCodecDecoder, error) {
	mat := createMatFromBytes(buf)
	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.avcodec_decoder_create(mat, hevcEnabled == "true", av1Enabled == "true")
	if decoder == nil {
		C.opencv_mat_release(mat)
		return nil, ErrInvalidImage
	}

	return &avCodecDecoder{
		decoder:      decoder,
		mat:          mat,
		buf:          buf,
		maybeMP4:     isMP4(buf),
		isStreamable: isStreamable(mat),
		hasSubtitles: hasSubtitles(decoder),
	}, nil
}

// createMatFromBytes creates an OpenCV matrix from a byte buffer.
// The matrix is created as a single-channel 8-bit unsigned type.
func createMatFromBytes(buf []byte) C.opencv_mat {
	return C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))
}

// hasSubtitles checks if the decoder has detected any subtitle streams.
func hasSubtitles(d C.avcodec_decoder) bool {
	return bool(C.avcodec_decoder_has_subtitles(d))
}

// isStreamable determines if the media content can be streamed.
func isStreamable(mat C.opencv_mat) bool {
	return bool(C.avcodec_decoder_is_streamable(mat))
}

// Description returns the format description of the media.
// Special handling is included to differentiate between MOV and MP4 formats.
func (d *avCodecDecoder) Description() string {
	fmt := C.GoString(C.avcodec_decoder_get_description(d.decoder))

	// differentiate MOV and MP4 based on magic
	if fmt == "MOV" && d.maybeMP4 {
		return "MP4"
	}

	return fmt
}

// HasSubtitles returns whether the media contains subtitle streams.
func (d *avCodecDecoder) HasSubtitles() bool {
	return d.hasSubtitles
}

// IsStreamable returns whether the media content can be streamed.
func (d *avCodecDecoder) IsStreamable() bool {
	return d.isStreamable
}

// BackgroundColor returns the default background color (white).
func (d *avCodecDecoder) BackgroundColor() uint32 {
	return 0xFFFFFFFF
}

// LoopCount returns the number of times the media should loop (0 for no looping).
func (d *avCodecDecoder) LoopCount() int {
	return 0
}

// ICC returns the ICC color profile data if present, or an empty slice if not.
func (d *avCodecDecoder) ICC() []byte {
	iccDst := make([]byte, ICCProfileBufferSize)
	iccLength := C.avcodec_decoder_get_icc(d.decoder, unsafe.Pointer(&iccDst[0]), C.size_t(cap(iccDst)))
	if iccLength <= 0 {
		return []byte{}
	}
	return iccDst[:iccLength]
}

// VideoCodec returns the video codec name (H264, HEVC, AV1, VP8, VP9, MPEG4, or Unknown).
func (d *avCodecDecoder) VideoCodec() string {
	return C.GoString(C.avcodec_decoder_get_video_codec(d.decoder))
}

// AudioCodec returns the audio codec name (AAC, MP3, FLAC, Vorbis, Opus, or Unknown).
func (d *avCodecDecoder) AudioCodec() string {
	return C.GoString(C.avcodec_decoder_get_audio_codec(d.decoder))
}

// Duration returns the total duration of the media content.
func (d *avCodecDecoder) Duration() time.Duration {
	return time.Duration(float64(C.avcodec_decoder_get_duration(d.decoder)) * float64(time.Second))
}

// Header returns the image metadata including dimensions, pixel format, and orientation.
// Frame count is 1 for single-frame mode, or estimated from duration and sample interval in multi-frame mode.
func (d *avCodecDecoder) Header() (*ImageHeader, error) {
	numFrames := 1
	if d.multiFrameMode && d.frameSampleInterval > 0 {
		// Estimate the number of frames based on duration and sample interval
		duration := float64(C.avcodec_decoder_get_duration(d.decoder))
		if duration > 0 {
			numFrames = int(duration/d.frameSampleInterval) + 1
		}
	}

	return &ImageHeader{
		width:         int(C.avcodec_decoder_get_width(d.decoder)),
		height:        int(C.avcodec_decoder_get_height(d.decoder)),
		pixelType:     PixelType(C.CV_8UC4),
		orientation:   ImageOrientation(C.avcodec_decoder_get_orientation(d.decoder)),
		numFrames:     numFrames,
		contentLength: len(d.buf),
	}, nil
}

// DecodeTo decodes the next frame into the provided Framebuffer.
// Returns io.EOF when no more frames are available.
func (d *avCodecDecoder) DecodeTo(f *Framebuffer) error {
	// In single-frame mode, only decode once
	if !d.multiFrameMode && d.hasDecoded {
		return io.EOF
	}
	h, err := d.Header()
	if err != nil {
		return err
	}
	err = f.resizeMat(h.Width(), h.Height(), h.PixelType())
	if err != nil {
		return err
	}

	// Call decode - it handles both single-frame and multi-frame modes internally
	ret := C.avcodec_decoder_decode(d.decoder, f.mat)
	if !ret {
		return io.EOF
	}

	// Set frame properties
	if d.multiFrameMode {
		// Get the frame delay from the decoder
		frameDelayMs := int(C.avcodec_decoder_get_frame_delay_ms(d.decoder))
		// Validate delay is reasonable (WebP supports up to 65535ms per frame)
		// Fall back to sample interval if delay is invalid
		if frameDelayMs <= 0 || frameDelayMs > 65535 {
			frameDelayMs = int(d.frameSampleInterval * 1000.0)
		}
		f.duration = time.Duration(frameDelayMs) * time.Millisecond
	} else {
		f.duration = time.Duration(0)
		d.hasDecoded = true
	}

	f.blend = NoBlend
	f.dispose = DisposeToBackgroundColor
	f.xOffset = 0
	f.yOffset = 0
	return nil
}

// SkipFrame attempts to skip the next frame, but is not supported by this decoder.
func (d *avCodecDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

// SetFrameSampleInterval configures the decoder to extract frames at the specified
// interval in seconds. This enables multi-frame extraction mode for videos.
func (d *avCodecDecoder) SetFrameSampleInterval(intervalSeconds float64) {
	d.multiFrameMode = true
	d.frameSampleInterval = intervalSeconds
	C.avcodec_decoder_set_frame_sample_interval(d.decoder, C.float(intervalSeconds))
}

// Close releases all resources associated with the decoder.
func (d *avCodecDecoder) Close() {
	C.avcodec_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

// init initializes the avcodec library when the package is loaded.
func init() {
	C.avcodec_init()
}
