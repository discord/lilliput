package lilliput

// #include "giflib.hpp"
import "C"

import (
	"errors"
	"io"
	"sync/atomic"
	"time"
	"unsafe"
)

// gifDecoder implements image decoding for GIF format
type gifDecoder struct {
	decoder           C.giflib_decoder
	mat               C.opencv_mat
	buf               []byte
	frameIndex        int
	loopCount         int
	frameCount        int
	animationInfoRead bool
	bgRed             uint8
	bgGreen           uint8
	bgBlue            uint8
	bgAlpha           uint8
}

// gifEncoder implements image encoding for GIF format
type gifEncoder struct {
	encoder    C.giflib_encoder
	decoder    C.giflib_decoder
	buf        []byte
	frameIndex int
	hasFlushed bool
}

const defaultMaxFrameDimension = 10000

var (
	gifMaxFrameDimension uint64

	ErrGifEncoderNeedsDecoder = errors.New("GIF encoder needs decoder used to create image")
)

// SetGIFMaxFrameDimension sets the largest GIF width/height that can be decoded.
// This helps prevent loading extremely large GIF images that could exhaust memory.
func SetGIFMaxFrameDimension(dim uint64) {
	// TODO we should investigate if this can be removed/become a mat check in decoder
	atomic.StoreUint64(&gifMaxFrameDimension, dim)
}

// newGifDecoder creates a new GIF decoder from the provided byte buffer.
// Returns an error if the buffer is too small or contains invalid GIF data.
func newGifDecoder(buf []byte) (*gifDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.giflib_decoder_create(mat)
	if decoder == nil {
		return nil, ErrInvalidImage
	}

	return &gifDecoder{
		decoder:    decoder,
		mat:        mat,
		buf:        buf,
		frameIndex: 0,
	}, nil
}

// Header returns the image header information including dimensions and pixel format.
func (d *gifDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width:         int(C.giflib_decoder_get_width(d.decoder)),
		height:        int(C.giflib_decoder_get_height(d.decoder)),
		pixelType:     PixelType(C.CV_8UC4),
		orientation:   OrientationTopLeft,
		numFrames:     d.FrameCount(),
		contentLength: len(d.buf),
	}, nil
}

// FrameHeader returns the current frame's header information.
func (d *gifDecoder) FrameHeader() (*ImageHeader, error) {
	return &ImageHeader{
		width:         int(C.giflib_decoder_get_frame_width(d.decoder)),
		height:        int(C.giflib_decoder_get_frame_height(d.decoder)),
		pixelType:     PixelType(C.CV_8UC4),
		orientation:   OrientationTopLeft,
		numFrames:     1,
		contentLength: len(d.buf),
	}, nil
}

// Close releases resources associated with the decoder.
func (d *gifDecoder) Close() {
	C.giflib_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

// Description returns the image format description ("GIF").
func (d *gifDecoder) Description() string {
	return "GIF"
}

// IsStreamable returns whether the format supports streaming decoding.
func (d *gifDecoder) IsStreamable() bool {
	return true
}

// HasSubtitles returns whether the format supports subtitles.
func (d *gifDecoder) HasSubtitles() bool {
	return false
}

// ICC returns the ICC color profile data, if any.
func (d *gifDecoder) ICC() []byte {
	return []byte{}
}

// Duration returns the total duration of the GIF animation.
func (d *gifDecoder) Duration() time.Duration {
	return time.Duration(0)
}

// BackgroundColor returns the GIF background color as a 32-bit RGBA value.
func (d *gifDecoder) BackgroundColor() uint32 {
	d.readAnimationInfo()
	return uint32(d.bgRed)<<16 | uint32(d.bgGreen)<<8 | uint32(d.bgBlue) | uint32(d.bgAlpha)<<24
}

// readAnimationInfo reads and caches GIF animation metadata like loop count and frame count.
// This is done lazily since reading extension blocks is relatively expensive.
func (d *gifDecoder) readAnimationInfo() {
	if !d.animationInfoRead {
		info := C.giflib_decoder_get_animation_info(d.decoder)
		d.loopCount = int(info.loop_count)
		d.frameCount = int(info.frame_count)
		d.animationInfoRead = true
		d.bgRed = uint8(info.bg_red)
		d.bgGreen = uint8(info.bg_green)
		d.bgBlue = uint8(info.bg_blue)
		d.bgAlpha = uint8(info.bg_alpha)
	}
}

// LoopCount returns the number of times the GIF animation should loop.
// A value of 0 means loop forever.
func (d *gifDecoder) LoopCount() int {
	d.readAnimationInfo()
	return d.loopCount
}

// FrameCount returns the total number of frames in the GIF.
func (d *gifDecoder) FrameCount() int {
	d.readAnimationInfo()
	return d.frameCount
}

// DecodeTo decodes the next GIF frame into the provided Framebuffer.
// Returns io.EOF when all frames have been decoded.
func (d *gifDecoder) DecodeTo(f *Framebuffer) error {
	h, err := d.Header()
	if err != nil {
		return err
	}

	err = f.resizeMat(h.Width(), h.Height(), h.PixelType())
	if err != nil {
		return err
	}

	nextFrameResult := int(C.giflib_decoder_decode_frame_header(d.decoder))
	if nextFrameResult == C.giflib_decoder_eof {
		return io.EOF
	}
	if nextFrameResult == C.giflib_decoder_error {
		return ErrInvalidImage
	}

	frameHeader, err := d.FrameHeader()
	if err != nil {
		return ErrInvalidImage
	}
	maxDim := int(atomic.LoadUint64(&gifMaxFrameDimension))
	if frameHeader.Width() > maxDim || frameHeader.Height() > maxDim {
		return ErrInvalidImage
	}

	ret := C.giflib_decoder_decode_frame(d.decoder, f.mat)
	if !ret {
		return ErrDecodingFailed
	}
	f.duration = time.Duration(C.giflib_decoder_get_prev_frame_delay(d.decoder)) * 10 * time.Millisecond
	f.blend = NoBlend
	f.dispose = DisposeMethod(C.giflib_decoder_get_prev_frame_disposal(d.decoder))
	f.xOffset = 0
	f.yOffset = 0
	d.frameIndex++
	return nil
}

// SkipFrame skips decoding of the next frame.
// Returns io.EOF when all frames have been skipped.
func (d *gifDecoder) SkipFrame() error {
	nextFrameResult := int(C.giflib_decoder_skip_frame(d.decoder))

	if nextFrameResult == C.giflib_decoder_eof {
		return io.EOF
	}
	if nextFrameResult == C.giflib_decoder_error {
		return ErrInvalidImage
	}

	return nil
}

// newGifEncoder creates a new GIF encoder that will write to the provided buffer.
// Requires the original decoder that was used to decode the source GIF.
func newGifEncoder(decodedBy Decoder, buf []byte) (*gifEncoder, error) {
	// we must have a decoder since we can't build our own palettes
	// so if we don't get a gif decoder, bail out
	if decodedBy == nil {
		return nil, ErrGifEncoderNeedsDecoder
	}

	gifDecoder, ok := decodedBy.(*gifDecoder)
	if !ok {
		return nil, ErrGifEncoderNeedsDecoder
	}

	buf = buf[:1]
	enc := C.giflib_encoder_create(unsafe.Pointer(&buf[0]), C.size_t(cap(buf)))
	if enc == nil {
		return nil, ErrBufTooSmall
	}

	return &gifEncoder{
		encoder:    enc,
		decoder:    gifDecoder.decoder,
		buf:        buf,
		frameIndex: 0,
	}, nil
}

// Encode encodes a frame into the GIF. If f is nil, flushes any remaining data
// and returns the complete encoded GIF. Returns io.EOF after flushing.
func (e *gifEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if e.hasFlushed {
		return nil, io.EOF
	}

	if f == nil {
		ret := C.giflib_encoder_flush(e.encoder, e.decoder)
		if !ret {
			return nil, ErrInvalidImage
		}
		e.hasFlushed = true

		len := C.int(C.giflib_encoder_get_output_length(e.encoder))

		return e.buf[:len], nil
	}

	if e.frameIndex == 0 {
		// first run setup
		// TODO figure out actual gif width/height?
		C.giflib_encoder_init(e.encoder, e.decoder, C.int(f.Width()), C.int(f.Height()))
	}

	if !C.giflib_encoder_encode_frame(e.encoder, e.decoder, f.mat) {
		return nil, ErrInvalidImage
	}

	e.frameIndex++

	return nil, nil
}

// Close releases resources associated with the encoder.
func (e *gifEncoder) Close() {
	C.giflib_encoder_release(e.encoder)
}

// init initializes the GIF decoder with default settings
func init() {
	SetGIFMaxFrameDimension(defaultMaxFrameDimension)
}
