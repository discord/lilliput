package lilliput

// #include "png.hpp"
// #include "opencv.hpp"
import "C"

import (
	"io"
	"unsafe"
)

// pngEncoder implements the Encoder interface for PNG images.
type pngEncoder struct {
	encoder C.png_encoder
	dstBuf  []byte
}

// newPngEncoder creates a new PNG encoder using the provided decoder for metadata
// and destination buffer for the encoded output.
func newPngEncoder(decodedBy Decoder, dstBuf []byte) (*pngEncoder, error) {
	dstBuf = dstBuf[:1]

	// Get ICC profile if available
	var icc []byte
	if decodedBy != nil {
		icc = decodedBy.ICC()
	}

	var enc C.png_encoder
	if len(icc) > 0 {
		enc = C.png_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)),
			unsafe.Pointer(&icc[0]), C.size_t(len(icc)))
	} else {
		enc = C.png_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)), nil, 0)
	}

	if enc == nil {
		return nil, ErrInvalidImage
	}

	return &pngEncoder{
		encoder: enc,
		dstBuf:  dstBuf,
	}, nil
}

// Encode encodes a frame into PNG format.
// Returns the encoded data or an error.
// The opt parameter allows specifying encoding options as key-value pairs.
func (e *pngEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if f == nil {
		return nil, io.EOF
	}

	// Validate and enforce parameter bounds
	for k, v := range opt {
		if k == PngCompression && (v < 0 || v > 9) {
			return nil, ErrInvalidParam
		}
	}

	// Convert opt map to C array
	var optList []C.int
	var firstOpt *C.int
	for k, v := range opt {
		optList = append(optList, C.int(k))
		optList = append(optList, C.int(v))
	}
	if len(optList) > 0 {
		firstOpt = (*C.int)(unsafe.Pointer(&optList[0]))
	}

	// Get framebuffer info
	var width, height C.int
	var stride C.size_t
	var data unsafe.Pointer
	var channels C.int

	if f.mat != nil {
		width = C.opencv_mat_get_width(f.mat)
		height = C.opencv_mat_get_height(f.mat)
		stride = C.opencv_mat_get_step(f.mat)
		data = C.opencv_mat_get_data(f.mat)
		channels = C.int(f.PixelType().Channels())
	}
	// If f.mat is nil, all values remain zero/nil, and C encoder will return appropriate error

	var length C.size_t
	result := C.png_encoder_encode(e.encoder, data, width, height, channels, stride,
		firstOpt, C.size_t(len(optList)), &length)

	if err := handlePngError(result); err != nil {
		return nil, err
	}

	return e.dstBuf[:length], nil
}

// Close releases all resources associated with the encoder.
func (e *pngEncoder) Close() {
	C.png_encoder_release(e.encoder)
}

// handlePngError converts PNG error codes to Go errors.
func handlePngError(code C.int) error {
	switch code {
	case C.L_PNG_SUCCESS:
		return nil
	case C.L_PNG_ERROR_BUFFER_TOO_SMALL:
		return ErrBufTooSmall
	case C.L_PNG_ERROR_INVALID_DIMENSIONS:
		return ErrFrameBufNoPixels
	case C.L_PNG_ERROR_NULL_MATRIX:
		return ErrInvalidImage
	case C.L_PNG_ERROR_INVALID_CHANNEL_COUNT:
		return ErrInvalidImage
	case C.L_PNG_ERROR_INVALID_ARG:
		return ErrInvalidParam
	default:
		return ErrInvalidImage
	}
}
