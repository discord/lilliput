package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++11
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lopencv_core -lopencv_imgproc -lwebp -lwebpmux
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/share/OpenCV/3rdparty/lib
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib
// #include "webp.hpp"
// #include "webp.hpp"
import "C"

import (
	"io"
	"time"
	"unsafe"
)

type webpDecoder struct {
	decoder C.webp_decoder
	mat     C.opencv_mat
	buf     []byte
}

type webpEncoder struct {
	encoder    C.webp_encoder
	dstBuf     []byte
	icc        []byte
	isAnimated bool
	frameIndex int
	hasFlushed bool
}

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

func (d *webpDecoder) Close() {
	C.webp_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

func (d *webpDecoder) Description() string {
	return "WEBP"
}

func (d *webpDecoder) Duration() time.Duration {
	return time.Duration(0)
}

func (d *webpDecoder) PreviousFrameDelay() time.Duration {
	return time.Duration(C.webp_decoder_get_prev_frame_delay(d.decoder)) * time.Millisecond
}

func (d *webpDecoder) PreviousFrameBlend() BlendMethod {
	return BlendMethod(C.webp_decoder_get_prev_frame_blend(d.decoder))
}

func (d *webpDecoder) PreviousFrameDispose() DisposeMethod {
	return DisposeMethod(C.webp_decoder_get_prev_frame_dispose(d.decoder))
}

func (d *webpDecoder) PreviousFrameXOffset() int {
	return int(C.webp_decoder_get_prev_frame_x_offset(d.decoder))
}

func (d *webpDecoder) PreviousFrameYOffset() int {
	return int(C.webp_decoder_get_prev_frame_y_offset(d.decoder))
}

func (d *webpDecoder) HasSubtitles() bool {
	return false
}

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

func (d *webpDecoder) ICC() []byte {
	iccDst := make([]byte, 8192)
	iccLength := C.webp_decoder_get_icc(d.decoder, unsafe.Pointer(&iccDst[0]), C.size_t(cap(iccDst)))
	return iccDst[:iccLength]
}

func (d *webpDecoder) BackgroundColor() uint32 {
	return uint32(C.webp_decoder_get_bg_color(d.decoder))
}

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

func (d *webpDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

func newWebpEncoder(decodedBy Decoder, dstBuf []byte) (*webpEncoder, error) {
	dstBuf = dstBuf[:1]
	icc := decodedBy.ICC()
	bgColor := decodedBy.BackgroundColor()

	var enc C.webp_encoder
	if len(icc) > 0 {
		enc = C.webp_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)), unsafe.Pointer(&icc[0]), C.size_t(len(icc)), C.uint32_t(bgColor))
	} else {
		enc = C.webp_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)), nil, 0, C.uint32_t(bgColor))
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

func (e *webpEncoder) Close() {
	C.webp_encoder_release(e.encoder)
}
