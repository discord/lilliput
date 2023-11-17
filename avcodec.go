package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++11
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS: -lswscale -lavformat -lavcodec -lavfilter -lavutil -lbz2 -lz
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib
// #include "avcodec.hpp"
import "C"

import (
	"io"
	"time"
	"unsafe"
)

const probeBytesLimit = 32 * 1024
const atomHeaderSize = 8

type avCodecDecoder struct {
	decoder      C.avcodec_decoder
	mat          C.opencv_mat
	buf          []byte
	hasDecoded   bool
	maybeMP4     bool
	isStreamable bool
}

func newAVCodecDecoder(buf []byte) (*avCodecDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.avcodec_decoder_create(mat)
	if decoder == nil {
		C.opencv_mat_release(mat)
		return nil, ErrInvalidImage
	}

	isStreamable := C.avcodec_decoder_is_streamable(mat)

	return &avCodecDecoder{
		decoder:      decoder,
		mat:          mat,
		buf:          buf,
		maybeMP4:     isMP4(buf),
		isStreamable: bool(isStreamable),
	}, nil
}

func (d *avCodecDecoder) Description() string {
	fmt := C.GoString(C.avcodec_decoder_get_description(d.decoder))

	// differentiate MOV and MP4 based on magic
	if fmt == "MOV" && d.maybeMP4 {
		return "MP4"
	}

	return fmt
}

func (d *avCodecDecoder) IsStreamable() bool {
	return d.isStreamable
}

func (d *avCodecDecoder) Duration() time.Duration {
	return time.Duration(float64(C.avcodec_decoder_get_duration(d.decoder)) * float64(time.Second))
}

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
	d.hasDecoded = true
	return nil
}

func (d *avCodecDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

func (d *avCodecDecoder) Close() {
	C.avcodec_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

func init() {
	C.avcodec_init()
}
