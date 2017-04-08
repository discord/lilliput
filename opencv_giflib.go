package opencv

// #include "opencv_giflib.hpp"
// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo LDFLAGS: -lgif
// #cgo linux pkg-config: opencv
// #cgo darwin pkg-config: opencv
import "C"

import (
	"io"
	"unsafe"
)

type GifDecoder struct {
	decoder    C.giflib_Decoder
	mat        C.opencv_Mat
	frameIndex int
	hasSlurped bool
}

func newGifDecoder(buf []byte) (*GifDecoder, error) {
	mat := C.opencv_createMatFromData(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.giflib_createDecoder(mat)
	if decoder == nil {
		return nil, ErrInvalidImage
	}

	return &GifDecoder{
		decoder:    decoder,
		mat:        mat,
		frameIndex: 0,
	}, nil
}

func (d *GifDecoder) Header() (*ImageHeader, error) {
	if !d.hasSlurped {
		ret := C.giflib_decoder_slurp(d.decoder)
		if !ret {
			return nil, ErrDecodingFailed
		}
		d.hasSlurped = true
	}

	return &ImageHeader{
		width:       int(C.giflib_get_decoder_width(d.decoder)),
		height:      int(C.giflib_get_decoder_height(d.decoder)),
		pixelType:   PixelType(C.CV_8UC4),
		orientation: OrientationTopLeft,
		numFrames:   int(C.giflib_get_decoder_num_frames(d.decoder)),
	}, nil
}

func (d *GifDecoder) Close() {
	C.giflib_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
}

func (d *GifDecoder) Description() string {
	return "GIF"
}

func (d *GifDecoder) DecodeTo(f *Framebuffer) error {
	numFrames := int(C.giflib_get_decoder_num_frames(d.decoder))
	if d.frameIndex == numFrames {
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

	ret := C.giflib_decoder_decode(d.decoder, C.int(d.frameIndex), f.mat)
	if !ret {
		return ErrDecodingFailed
	}
	d.frameIndex++
	return nil
}
