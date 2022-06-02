package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++11
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -ljpeg -lpng -lwebp -lippicv -lz -lgif
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/share/OpenCV/3rdparty/lib
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib
// #include "apng.hpp"
import "C"

import (
	"io"
	"sync/atomic"
	"time"
	"unsafe"
)

type apngDecoder struct {
	decoder    C.apng_decoder
	mat        C.opencv_mat
	buf        []byte
	frameIndex int
}

type apngEncoder struct {
	encoder    C.apng_encoder
	buf        []byte
	totalFrames int
	frameIndex int
	hasFlushed bool
}

var (
	apngMaxFrameDimension uint64
)

// SetAPNGMaxFrameDimension sets the largest APNG width/height that can be
// decoded
func SetAPNGMaxFrameDimension(dim uint64) {
	// TODO we should investigate if this can be removed/become a mat check in decoder
	atomic.StoreUint64(&apngMaxFrameDimension, dim)
}

func newApngDecoder(buf []byte) (*apngDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.apng_decoder_create(mat)
	if decoder == nil {
		return nil, ErrInvalidImage
	}

	return &apngDecoder{
		decoder:    decoder,
		mat:        mat,
		buf:        buf,
		frameIndex: 0,
	}, nil
}

func (d *apngDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width:       int(C.apng_decoder_get_width(d.decoder)),
		height:      int(C.apng_decoder_get_height(d.decoder)),
		pixelType:   PixelType(C.CV_8UC4),
		orientation: OrientationTopLeft,
		numFrames:   int(C.apng_decoder_get_num_frames(d.decoder)),
	}, nil
}

func (d *apngDecoder) FrameHeader() (*ImageHeader, error) {
	return &ImageHeader{
		width:       int(C.apng_decoder_get_frame_width(d.decoder)),
		height:      int(C.apng_decoder_get_frame_height(d.decoder)),
		pixelType:   PixelType(C.CV_8UC4),
		orientation: OrientationTopLeft,
		numFrames:   1,
	}, nil
}

func (d *apngDecoder) Close() {
	C.apng_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

func (d *apngDecoder) Description() string {
	return "APNG"
}

func (d *apngDecoder) Duration() time.Duration {
	return time.Duration(0)
}

func (d *apngDecoder) DecodeTo(f *Framebuffer) error {
	h, err := d.Header()
	if err != nil {
		return err
	}

	err = f.resizeMat(h.Width(), h.Height(), h.PixelType())
	if err != nil {
		return err
	}

	nextFrameResult := int(C.apng_decoder_decode_frame_header(d.decoder))
	if nextFrameResult == C.apng_decoder_eof {
		return io.EOF
	}
	if nextFrameResult == C.apng_decoder_error {
		return ErrInvalidImage
	}

	frameHeader, err := d.FrameHeader()
	if err != nil {
		return ErrInvalidImage
	}
	maxDim := int(atomic.LoadUint64(&apngMaxFrameDimension))
	if frameHeader.Width() > maxDim || frameHeader.Height() > maxDim {
		return ErrInvalidImage
	}

	ret := C.apng_decoder_decode_frame(d.decoder, f.mat)
	if !ret {
		return ErrDecodingFailed
	}
	num := C.apng_decoder_get_prev_frame_delay_num(d.decoder)
	den := C.apng_decoder_get_prev_frame_delay_den(d.decoder)
	f.duration = time.Second * time.Duration(num) / time.Duration(den)
	d.frameIndex++
	return nil
}

func (d *apngDecoder) SkipFrame() error {
	nextFrameResult := int(C.apng_decoder_skip_frame(d.decoder))

	if nextFrameResult == C.apng_decoder_eof {
		return io.EOF
	}
	if nextFrameResult == C.apng_decoder_error {
		return ErrInvalidImage
	}

	return nil
}

func newApngEncoder(decodedBy Decoder, buf []byte) (*apngEncoder, error) {
	buf = buf[:1]
	enc := C.apng_encoder_create(unsafe.Pointer(&buf[0]), C.size_t(cap(buf)))
	if enc == nil {
		return nil, ErrBufTooSmall
	}

	hdr, err := decodedBy.Header()
	if err != nil {
		return nil, err
	}

	return &apngEncoder{
		encoder:    enc,
		buf:        buf,
		totalFrames: hdr.numFrames, // todo: this is not necessarily accurate
		frameIndex: 0,
	}, nil
}

func (e *apngEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if e.hasFlushed {
		return nil, io.EOF
	}

	if f == nil {
		ret := C.apng_encoder_flush(e.encoder)
		if !ret {
			return nil, ErrInvalidImage
		}
		e.hasFlushed = true

		len := C.int(C.apng_encoder_get_output_length(e.encoder))

		return e.buf[:len], nil
	}

	if e.frameIndex == 0 {
		C.apng_encoder_init(e.encoder, C.int(f.Width()), C.int(f.Height()), C.int(e.totalFrames))
	}

	if !C.apng_encoder_encode_frame(e.encoder, f.mat, C.int(f.duration.Milliseconds())) {
		return nil, ErrInvalidImage
	}

	e.frameIndex++

	return nil, nil
}

func (e *apngEncoder) Close() {
	C.apng_encoder_release(e.encoder)
}

func init() {
	SetAPNGMaxFrameDimension(defaultMaxFrameDimension)
}
