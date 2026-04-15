package lilliput

// #include "avcodec.hpp"
import "C"

import (
	"errors"
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
	decoder      C.avcodec_decoder
	mat          C.opencv_mat
	buf          []byte
	hasDecoded   bool
	maybeMP4     bool
	isStreamable bool
	hasSubtitles bool
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
// Frame count is always 1 since it requires the entire buffer to be decoded.
func (d *avCodecDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width:         int(C.avcodec_decoder_get_width(d.decoder)),
		height:        int(C.avcodec_decoder_get_height(d.decoder)),
		pixelType:     PixelType(C.CV_8UC4),
		orientation:   ImageOrientation(C.avcodec_decoder_get_orientation(d.decoder)),
		numFrames:     1,
		contentLength: len(d.buf),
	}, nil
}

// DecodeTo decodes the next frame into the provided Framebuffer.
// Returns io.EOF when no more frames are available.
func (d *avCodecDecoder) DecodeTo(f *Framebuffer) error {
	if d.hasDecoded {
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
	ret := C.avcodec_decoder_decode(d.decoder, f.mat)
	if !ret {
		return ErrDecodingFailed
	}
	f.blend = NoBlend
	f.dispose = DisposeToBackgroundColor
	f.duration = time.Duration(0)
	f.xOffset = 0
	f.yOffset = 0
	d.hasDecoded = true
	return nil
}

// SkipFrame attempts to skip the next frame, but is not supported by this decoder.
func (d *avCodecDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

// Close releases all resources associated with the decoder.
func (d *avCodecDecoder) Close() {
	C.avcodec_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

// KeyframeEntry represents a keyframe from the video's index (moov atom).
// Contains the timestamp, byte offset into mdat, and compressed size.
type KeyframeEntry struct {
	TimestampUs int64
	ByteOffset  int64
	Size        int32
}

// KeyframeCount returns the number of keyframes in the video stream's index.
func (d *avCodecDecoder) KeyframeCount() (int, error) {
	count := C.avcodec_decoder_get_keyframe_count(d.decoder)
	if count < 0 {
		return 0, errors.New("failed to get keyframe count")
	}
	return int(count), nil
}

// Keyframes returns all keyframe entries from the video stream's index.
func (d *avCodecDecoder) Keyframes() ([]KeyframeEntry, error) {
	count, err := d.KeyframeCount()
	if err != nil {
		return nil, err
	}
	if count == 0 {
		return nil, nil
	}

	raw := make([]C.avcodec_keyframe_entry, count)
	got := C.avcodec_decoder_get_keyframes(d.decoder, &raw[0], C.int(count))
	if got < 0 {
		return nil, errors.New("failed to get keyframe entries")
	}

	entries := make([]KeyframeEntry, int(got))
	for i := 0; i < int(got); i++ {
		entries[i] = KeyframeEntry{
			TimestampUs: int64(raw[i].timestamp_us),
			ByteOffset:  int64(raw[i].byte_offset),
			Size:        int32(raw[i].size),
		}
	}
	return entries, nil
}

// CodecID returns the AVCodecID for the video stream.
func (d *avCodecDecoder) CodecID() (int, error) {
	id := C.avcodec_decoder_get_codec_id(d.decoder)
	if id < 0 {
		return 0, errors.New("failed to get codec id")
	}
	return int(id), nil
}

// Extradata returns the codec extradata (e.g. SPS/PPS for H.264).
func (d *avCodecDecoder) Extradata() ([]byte, error) {
	size := C.avcodec_decoder_get_extradata(d.decoder, nil, 0)
	if size < 0 {
		return nil, errors.New("failed to get extradata size")
	}
	if size == 0 {
		return nil, nil
	}

	buf := make([]byte, int(size))
	copied := C.avcodec_decoder_get_extradata(d.decoder, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))
	if copied < 0 {
		return nil, errors.New("failed to copy extradata")
	}
	return buf[:int(copied)], nil
}

// bgraStrideBufSize returns the buffer size needed for a BGRA framebuffer at
// the given dimensions, accounting for the 32-pixel row stride alignment that
// the C layer's sws_scale path requires for SIMD.
func bgraStrideBufSize(width, height int) int {
	paddedWidth := width
	if width%32 != 0 {
		paddedWidth = width + 32 - (width % 32)
	}
	return paddedWidth * height * 4
}

// DecodeRawKeyframe decodes a raw keyframe chunk into BGRA pixels in the provided Framebuffer.
// The chunk is raw compressed data from the mdat region (via range request).
// codecID, extradata, sourceWidth, and sourceHeight come from the moov parse phase.
// thumbWidth and thumbHeight specify the desired output dimensions.
func DecodeRawKeyframe(codecID int, extradata []byte, sourceWidth, sourceHeight int, chunk []byte, thumbWidth, thumbHeight int, dst *Framebuffer) error {
	if requiredSize := bgraStrideBufSize(thumbWidth, thumbHeight); len(dst.buf) < requiredSize {
		dst.buf = make([]byte, requiredSize)
	}

	err := dst.resizeMat(thumbWidth, thumbHeight, PixelType(C.CV_8UC4))
	if err != nil {
		return err
	}

	var extradataPtr unsafe.Pointer
	extradataSize := C.int(0)
	if len(extradata) > 0 {
		extradataPtr = unsafe.Pointer(&extradata[0])
		extradataSize = C.int(len(extradata))
	}

	ok := C.avcodec_decode_raw_keyframe(
		C.int(codecID),
		extradataPtr,
		extradataSize,
		C.int(sourceWidth),
		C.int(sourceHeight),
		unsafe.Pointer(&chunk[0]),
		C.int(len(chunk)),
		dst.mat,
	)
	if !ok {
		return ErrDecodingFailed
	}
	return nil
}

// init initializes the avcodec library when the package is loaded.
func init() {
	C.avcodec_init()
}
