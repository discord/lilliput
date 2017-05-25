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
	"unsafe"
)

type AVCodecDecoder struct {
	decoder    C.avcodec_decoder
	mat        C.opencv_mat
	hasDecoded bool
}

func newAVCodecDecoder(buf []byte) (*AVCodecDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.avcodec_decoder_create(mat)
	if decoder == nil {
		C.opencv_mat_release(mat)
		return nil, ErrInvalidImage
	}

	return &AVCodecDecoder{
		decoder: decoder,
		mat:     mat,
	}, nil
}

func (d *AVCodecDecoder) Description() string {
	return C.GoString(C.avcodec_decoder_get_description(d.decoder))
}

func (d *AVCodecDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width:       int(C.avcodec_decoder_get_width(d.decoder)),
		height:      int(C.avcodec_decoder_get_height(d.decoder)),
		pixelType:   PixelType(C.CV_8UC4),
		orientation: OrientationTopLeft,
		numFrames:   1,
	}, nil
}

func (d *AVCodecDecoder) DecodeTo(f *Framebuffer) error {
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

func (d *AVCodecDecoder) Close() {
	C.avcodec_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
}

func init() {
	C.avcodec_init()
}
