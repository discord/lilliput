package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++11
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -ljpeg -lpng -lwebp -lippicv -lz
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/share/OpenCV/3rdparty/lib
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib
// #include "thumbhash.hpp"
import "C"

import (
	"io"
	"unsafe"
)

type thumbhashEncoder struct {
	encoder C.thumbhash_encoder
	buf     []byte
}

func newThumbhashEncoder(decodedBy Decoder, buf []byte) (*thumbhashEncoder, error) {
	buf = buf[:1]
	enc := C.thumbhash_encoder_create(unsafe.Pointer(&buf[0]), C.size_t(cap(buf)))
	if enc == nil {
		return nil, ErrBufTooSmall
	}
	return &thumbhashEncoder{
		encoder: enc,
		buf:     buf,
	}, nil
}

func (e *thumbhashEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if f == nil {
		return nil, io.EOF
	}

	length := C.thumbhash_encoder_encode(e.encoder, f.mat)
	if length <= 0 {
		return nil, ErrInvalidImage
	}

	return e.buf[:length], nil
}

func (e *thumbhashEncoder) Close() {
	C.thumbhash_encoder_release(e.encoder)
}
