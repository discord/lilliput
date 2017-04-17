package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++14
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -ljpeg -lpng -lwebp -lippicv -lz -lgif
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/share/OpenCV/3rdparty/lib -framework Accelerate
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib
// #include "opencv_giflib.hpp"
import "C"

import (
	"errors"
	"io"
	"unsafe"
)

type GifDecoder struct {
	decoder    C.giflib_decoder
	mat        C.opencv_mat
	frameIndex int
	hasSlurped bool
}

type GifEncoder struct {
	encoder    C.giflib_encoder
	buf        *OutputBuffer
	frameIndex int
	hasSpewed  bool
}

var ErrGifEncoderNeedsDecoder = errors.New("GIF encoder needs decoder used to create image")

func newGifDecoder(buf []byte) (*GifDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.giflib_decoder_create(mat)
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

func (d *GifDecoder) slurp() error {
	if !d.hasSlurped {
		ret := C.giflib_decoder_slurp(d.decoder)
		if !ret {
			return ErrDecodingFailed
		}
		d.hasSlurped = true
	}
	return nil
}

func (d *GifDecoder) DecodeTo(f *Framebuffer) error {
	h, err := d.Header()
	if err != nil {
		return err
	}

	err = d.slurp()
	if err != nil {
		return err
	}

	numFrames := int(C.giflib_get_decoder_num_frames(d.decoder))
	if d.frameIndex == numFrames {
		return io.EOF
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

func newGifEncoder(decodedBy Decoder, buf *OutputBuffer) (*GifEncoder, error) {
	// we must have a decoder since we can't build our own palettes
	// so if we don't get a gif decoder, bail out
	if decodedBy == nil {
		return nil, ErrGifEncoderNeedsDecoder
	}

	gifDecoder, ok := decodedBy.(*GifDecoder)
	if !ok {
		return nil, ErrGifEncoderNeedsDecoder
	}

	err := gifDecoder.slurp()
	if err != nil {
		return nil, err
	}

	enc := C.giflib_encoder_create(buf.vec, gifDecoder.decoder)
	if enc == nil {
		return nil, ErrBufTooSmall
	}

	return &GifEncoder{
		encoder:    enc,
		buf:        buf,
		frameIndex: 0,
	}, nil
}

func (e *GifEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if e.hasSpewed {
		return nil, io.EOF
	}

	if f == nil {
		ret := C.giflib_encoder_spew(e.encoder)
		if !ret {
			return nil, ErrInvalidImage
		}
		e.hasSpewed = true

		err := e.buf.copyOutput()
		if err != nil {
			return nil, err
		}
		return e.buf.bytes, nil
	}

	if e.frameIndex == 0 {
		// first run setup
		// TODO figure out actual gif width/height?
		C.giflib_encoder_init(e.encoder, C.int(f.Width()), C.int(f.Height()))
	}

	if !C.giflib_encoder_encode_frame(e.encoder, C.int(e.frameIndex), f.mat) {
		return nil, ErrInvalidImage
	}

	e.frameIndex++

	return nil, nil
}

func (e *GifEncoder) Close() {
	C.giflib_encoder_release(e.encoder)
}
