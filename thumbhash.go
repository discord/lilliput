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
	"fmt"
	"io"
	"unsafe"
)

const maxDimension = 100
const maxChannels = 4
const maxFrameSize = maxDimension * maxDimension * maxChannels
const maxHashSize = 25

type thumbhashEncoder struct {
	encoder       C.thumbhash_encoder
	hashBuf       []byte
	hashBufSize   int
	resizeBuf     []byte
	resizeBufSize int
	colorBuf      []byte
	colorBufSize  int
}

func newThumbhashEncoder(decodedBy Decoder, buf []byte) (*thumbhashEncoder, error) {
	minSize := maxHashSize + maxFrameSize*2
	if cap(buf) < minSize {
		return nil, ErrBufTooSmall
	}
	resizeBuf := buf[:1]
	colorBuf := buf[maxFrameSize : maxFrameSize+1]
	hashBuf := buf[2*maxFrameSize : 2*maxFrameSize+1]

	enc := C.thumbhash_encoder_create(unsafe.Pointer(&hashBuf[0]), C.size_t(maxHashSize))
	if enc == nil {
		return nil, ErrBufTooSmall
	}
	return &thumbhashEncoder{
		encoder:       enc,
		hashBuf:       hashBuf,
		hashBufSize:   maxHashSize,
		resizeBuf:     resizeBuf,
		resizeBufSize: maxFrameSize,
		colorBuf:      colorBuf,
		colorBufSize:  maxFrameSize,
	}, nil
}

func (e *thumbhashEncoder) resizeIfNecessary(f *Framebuffer) (C.opencv_mat, error) {
	w := f.width
	h := f.height
	pixelType := f.pixelType

	if pixelType.Depth() > 8 {
		pixelType = PixelType(C.opencv_type_convert_depth(C.int(pixelType), C.CV_8U))
	}

	// Resize the image if it's too large
	if f.width > maxDimension || f.height > maxDimension {
		aspectRatio := float64(f.width) / float64(f.height)
		if f.width > f.height {
			w = maxDimension
			h = int(float64(w) / aspectRatio)
		} else {
			h = maxDimension
			w = int(float64(h) * aspectRatio)
		}
	}

	if w != f.width || h != f.height || pixelType != f.pixelType {
		newMat := C.opencv_mat_create_from_data(C.int(w), C.int(h), C.int(pixelType), unsafe.Pointer(&e.resizeBuf[0]), C.size_t(e.resizeBufSize))
		if newMat == nil {
			return nil, ErrBufTooSmall
		}
		C.opencv_mat_resize(f.mat, newMat, C.int(w), C.int(h), C.CV_INTER_AREA)
		return newMat, nil
	} else {
		return nil, nil
	}
}

func (e *thumbhashEncoder) convertColorIfNecessary(mat C.opencv_mat) (C.opencv_mat, error) {
	width := C.opencv_mat_get_width(mat)
	height := C.opencv_mat_get_height(mat)
	channels := C.opencv_mat_get_channels(mat)
	fmt.Printf("width: %d, height: %d, channels: %d\n", width, height, channels)

	conversion := -1
	switch channels {
	case 1:
		conversion = C.CV_COLOR_GRAY2RGBA
	case 3:
		conversion = C.CV_COLOR_RGB2RGBA
	case 4:
		break
	default:
		return nil, ErrInvalidImage
	}
	if conversion == -1 {
		return nil, nil
	}
	newMat := C.opencv_mat_create_from_data(C.int(width), C.int(height), C.int(C.CV_8UC4), unsafe.Pointer(&e.colorBuf[0]), C.size_t(e.colorBufSize))
	if newMat == nil {
		return nil, ErrBufTooSmall
	}
	C.opencv_mat_to_rgba(mat, newMat, C.int(conversion))
	return newMat, nil

}

func (e *thumbhashEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if f == nil {
		return nil, io.EOF
	}
	mat := f.mat

	width := C.opencv_mat_get_width(mat)
	height := C.opencv_mat_get_height(mat)
	channels := C.opencv_mat_get_channels(mat)
	depth := C.opencv_mat_get_depth(mat)
	fmt.Printf("Encode(): width: %d, height: %d, channels: %d, depth: %d\n", width, height, channels, depth)

	maybeMat, err := e.resizeIfNecessary(f)
	if err != nil {
		return nil, err
	}
	if maybeMat != nil {
		defer C.opencv_mat_release(maybeMat)
		mat = maybeMat
	}

	maybeMat2, err := e.convertColorIfNecessary(mat)
	if err != nil {
		return nil, err
	}
	if maybeMat2 != nil {
		defer C.opencv_mat_release(maybeMat2)
		mat = maybeMat2
	}
	length := C.thumbhash_encoder_encode(e.encoder, mat)
	if length <= 0 {
		return nil, ErrInvalidImage
	}

	return e.hashBuf[:length], nil
}

func (e *thumbhashEncoder) Close() {
	C.thumbhash_encoder_release(e.encoder)
}
