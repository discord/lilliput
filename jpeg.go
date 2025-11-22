package lilliput

// #include "jpeg.hpp"
// #include "opencv.hpp"
import "C"

import (
	"io"
	"unsafe"
)

// jpegEncoder implements the Encoder interface for JPEG images.
type jpegEncoder struct {
	encoder C.jpeg_encoder
	dstBuf  []byte
}

// newJpegEncoder creates a new JPEG encoder using the provided decoder for metadata
// and destination buffer for the encoded output.
func newJpegEncoder(decodedBy Decoder, dstBuf []byte) (*jpegEncoder, error) {
	dstBuf = dstBuf[:1]

	// Get ICC profile if available
	var icc []byte
	if decodedBy != nil {
		icc = decodedBy.ICC()
	}

	var enc C.jpeg_encoder
	if len(icc) > 0 {
		enc = C.jpeg_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)),
			unsafe.Pointer(&icc[0]), C.size_t(len(icc)))
	} else {
		enc = C.jpeg_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)), nil, 0)
	}

	if enc == nil {
		return nil, ErrInvalidImage
	}

	return &jpegEncoder{
		encoder: enc,
		dstBuf:  dstBuf,
	}, nil
}

// Encode encodes a frame into JPEG format.
// Returns the encoded data or an error.
// The opt parameter allows specifying encoding options as key-value pairs.
func (e *jpegEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if f == nil {
		return nil, io.EOF
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
	result := C.jpeg_encoder_encode(e.encoder, data, width, height, channels, stride,
		firstOpt, C.size_t(len(optList)), &length)

	if err := handleJpegError(result); err != nil {
		return nil, err
	}

	return e.dstBuf[:length], nil
}

// Close releases all resources associated with the encoder.
func (e *jpegEncoder) Close() {
	C.jpeg_encoder_release(e.encoder)
}

// handleJpegError converts JPEG error codes to Go errors.
func handleJpegError(code C.int) error {
	switch code {
	case C.L_JPEG_SUCCESS:
		return nil
	case C.L_JPEG_ERROR_BUFFER_TOO_SMALL:
		return ErrBufTooSmall
	case C.L_JPEG_ERROR_INVALID_DIMENSIONS:
		return ErrFrameBufNoPixels
	case C.L_JPEG_ERROR_NULL_MATRIX:
		return ErrInvalidImage
	case C.L_JPEG_ERROR_INVALID_CHANNEL_COUNT:
		return ErrInvalidImage
	case C.L_JPEG_ERROR_INVALID_ARG:
		return ErrInvalidParam
	default:
		return ErrInvalidImage
	}
}
