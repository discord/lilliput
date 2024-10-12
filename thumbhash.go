package lilliput

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
