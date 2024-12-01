package lilliput

// #include "webp.hpp"
import "C"

import (
	"io"
	"time"
	"unsafe"
)

// webpDecoder implements the Decoder interface for WebP images.
type webpDecoder struct {
	decoder C.webp_decoder
	mat     C.opencv_mat
	buf     []byte
}

// webpEncoder implements the Encoder interface for WebP images.
type webpEncoder struct {
	encoder    C.webp_encoder
	dstBuf     []byte
	icc        []byte
	isAnimated bool
	frameIndex int
	hasFlushed bool
}

// newWebpDecoder creates a new WebP decoder from the provided byte buffer.
// Returns an error if the buffer is too small or contains invalid WebP data.
func newWebpDecoder(buf []byte) (*webpDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.webp_decoder_create(mat)
	if decoder == nil {
		return nil, ErrInvalidImage
	}

	return &webpDecoder{
		decoder: decoder,
		mat:     mat,
		buf:     buf,
	}, nil
}

// Header returns the image metadata including dimensions, pixel type, and frame count.
func (d *webpDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width:         int(C.webp_decoder_get_width(d.decoder)),
		height:        int(C.webp_decoder_get_height(d.decoder)),
		pixelType:     PixelType(C.webp_decoder_get_pixel_type(d.decoder)),
		orientation:   OrientationTopLeft,
		numFrames:     int(C.webp_decoder_get_num_frames(d.decoder)),
		contentLength: len(d.buf),
	}, nil
}

// Close releases all resources associated with the decoder.
func (d *webpDecoder) Close() {
	C.webp_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

// Description returns the image format description ("WEBP").
func (d *webpDecoder) Description() string {
	return "WEBP"
}

// Duration returns the total duration of the WebP animation.
// Returns 0 for static images.
func (d *webpDecoder) Duration() time.Duration {
	return time.Duration(0)
}

// HasSubtitles returns whether the image contains subtitle data (always false for WebP).
func (d *webpDecoder) HasSubtitles() bool {
	return false
}

// IsStreamable returns whether the image format supports streaming (always false for WebP).
func (d *webpDecoder) IsStreamable() bool {
	return false
}

// hasReachedEndOfFrames checks if the decoder has reached the end of all frames.
func (d *webpDecoder) hasReachedEndOfFrames() bool {
	return C.webp_decoder_has_more_frames(d.decoder) == 0
}

// advanceFrameIndex advances the internal frame index for the next decoding call.
func (d *webpDecoder) advanceFrameIndex() {
	// Advance the frame index within the C++ decoder
	C.webp_decoder_advance_frame(d.decoder)
}

// ICC returns the ICC color profile data embedded in the WebP image.
func (d *webpDecoder) ICC() []byte {
	iccDst := make([]byte, 8192)
	iccLength := C.webp_decoder_get_icc(d.decoder, unsafe.Pointer(&iccDst[0]), C.size_t(cap(iccDst)))
	return iccDst[:iccLength]
}

// BackgroundColor returns the background color of the WebP image.
func (d *webpDecoder) BackgroundColor() uint32 {
	return uint32(C.webp_decoder_get_bg_color(d.decoder))
}

// LoopCount returns the number of times the animation should loop.
func (d *webpDecoder) LoopCount() int {
	return int(C.webp_decoder_get_loop_count(d.decoder))
}

// DecodeTo decodes the current frame into the provided Framebuffer.
// Returns io.EOF when all frames have been decoded.
// Returns ErrDecodingFailed if the frame cannot be decoded.
func (d *webpDecoder) DecodeTo(f *Framebuffer) error {
	if f == nil {
		return io.EOF
	}

	// Get image header information
	h, err := d.Header()
	if err != nil {
		return err
	}

	// Resize the framebuffer matrix to fit the image dimensions and pixel type
	err = f.resizeMat(h.Width(), h.Height(), h.PixelType())
	if err != nil {
		return err
	}

	// Decode the current frame into the framebuffer
	ret := C.webp_decoder_decode(d.decoder, f.mat)
	if !ret {
		// Check if the decoder has reached the end of the frames
		if d.hasReachedEndOfFrames() {
			return io.EOF
		}
		return ErrDecodingFailed
	}

	// Set the frame properties
	f.duration = time.Duration(C.webp_decoder_get_prev_frame_delay(d.decoder)) * time.Millisecond
	f.xOffset = int(C.webp_decoder_get_prev_frame_x_offset(d.decoder))
	f.yOffset = int(C.webp_decoder_get_prev_frame_y_offset(d.decoder))
	f.dispose = DisposeMethod(C.webp_decoder_get_prev_frame_dispose(d.decoder))
	f.blend = BlendMethod(C.webp_decoder_get_prev_frame_blend(d.decoder))

	// Advance to the next frame
	d.advanceFrameIndex()

	return nil
}

// SkipFrame is not supported for WebP images and always returns ErrSkipNotSupported.
func (d *webpDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

// newWebpEncoder creates a new WebP encoder using the provided decoder for metadata
// and destination buffer for the encoded output.
func newWebpEncoder(decodedBy Decoder, dstBuf []byte) (*webpEncoder, error) {
	dstBuf = dstBuf[:1]
	icc := decodedBy.ICC()
	bgColor := decodedBy.BackgroundColor()
	loopCount := decodedBy.LoopCount()

	var enc C.webp_encoder
	if len(icc) > 0 {
		enc = C.webp_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)), unsafe.Pointer(&icc[0]), C.size_t(len(icc)), C.uint32_t(bgColor), C.int(loopCount))
	} else {
		enc = C.webp_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)), nil, 0, C.uint32_t(bgColor), C.int(loopCount))
	}
	if enc == nil {
		return nil, ErrBufTooSmall
	}

	return &webpEncoder{
		encoder: enc,
		dstBuf:  dstBuf,
		icc:     icc,
	}, nil
}

// Encode encodes a frame into the WebP format.
// If f is nil, finalizes the WebP animation and returns the encoded data.
// Returns io.EOF after the animation has been finalized.
// The opt parameter allows specifying encoding options as key-value pairs.
func (e *webpEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if e.hasFlushed {
		return nil, io.EOF
	}

	if f == nil {
		// Finalize the WebP animation
		length := C.webp_encoder_flush(e.encoder)
		if length == 0 {
			return nil, ErrInvalidImage
		}

		e.hasFlushed = true
		return e.dstBuf[:length], nil
	}

	var optList []C.int
	var firstOpt *C.int
	for k, v := range opt {
		optList = append(optList, C.int(k))
		optList = append(optList, C.int(v))
	}
	if len(optList) > 0 {
		firstOpt = (*C.int)(unsafe.Pointer(&optList[0]))
	}

	// Encode the current frame
	frameDelay := int(f.duration.Milliseconds())
	length := C.webp_encoder_write(e.encoder, f.mat, firstOpt, C.size_t(len(optList)), C.int(frameDelay), C.int(f.blend), C.int(f.dispose), 0, 0)
	if length == 0 {
		return nil, ErrInvalidImage
	}

	e.frameIndex++

	return nil, nil
}

// Close releases all resources associated with the encoder.
func (e *webpEncoder) Close() {
	C.webp_encoder_release(e.encoder)
}
