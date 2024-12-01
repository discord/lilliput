package lilliput

// #include "thumbhash.hpp"
import "C"

import (
	"io"
	"unsafe"
)

// thumbhashEncoder handles the encoding of images into ThumbHash format.
// ThumbHash is a very compact representation of a placeholder for an image.
type thumbhashEncoder struct {
	encoder C.thumbhash_encoder
	buf     []byte
}

// newThumbhashEncoder creates a new ThumbHash encoder instance.
// It takes a decoder and a buffer as input, initializing the C-based encoder.
// Returns an error if the provided buffer is too small.
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

// Encode converts the given Framebuffer into a ThumbHash byte representation.
// The opt parameter allows passing encoding options as key-value pairs.
// Returns the encoded bytes or an error if the input is invalid.
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

// Close releases the resources associated with the ThumbHash encoder.
// This should be called when the encoder is no longer needed.
func (e *thumbhashEncoder) Close() {
	C.thumbhash_encoder_release(e.encoder)
}
