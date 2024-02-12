package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++11
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lopencv_core -lopencv_imgproc -lwebp -lwebpmux
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/share/OpenCV/3rdparty/lib
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib
// #include "webp.hpp"
import "C"

import (
	"io"
	"time"
	"unsafe"
)

type webpDecoder struct {
	decoder    C.webp_decoder
	mat        C.opencv_mat
	buf        []byte
}

type webpEncoder struct {
	encoder C.webp_encoder
	dstBuf  []byte
	icc     []byte
}

func newWebpDecoder(buf []byte) (*webpDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.webp_decoder_create(mat)
	if decoder == nil {
		return nil, ErrInvalidImage
	}

	return &webpDecoder{
		decoder:    decoder,
		mat:        mat,
		buf:        buf,
	}, nil
}

func (d *webpDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width:         int(C.webp_decoder_get_width(d.decoder)),
		height:        int(C.webp_decoder_get_height(d.decoder)),
		pixelType:     PixelType(C.webp_decoder_get_pixel_type(d.decoder)),
		orientation:   OrientationTopLeft,
		numFrames:     1,
		contentLength: len(d.buf),
	}, nil
}

func (d *webpDecoder) Close() {
	C.webp_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

func (d *webpDecoder) Description() string {
	return "WEBP"
}

func (d *webpDecoder) Duration() time.Duration {
	return time.Duration(0)
}

func (d *webpDecoder) HasSubtitles() bool {
	return false
}

func (d *webpDecoder) IsStreamable() bool {
	return false
}

func (d *webpDecoder) ICC() []byte {
	iccDst := make([]byte, 8192)
	iccLength := C.webp_decoder_get_icc(d.decoder, unsafe.Pointer(&iccDst[0]), C.size_t(cap(iccDst)))
	return iccDst[:iccLength]
}

func (d *webpDecoder) DecodeTo(f *Framebuffer) error {
	h, err := d.Header()
	if err != nil {
		return err
	}
	err = f.resizeMat(h.Width(), h.Height(), h.PixelType())
	if err != nil {
		return err
	}
	ret := C.webp_decoder_decode(d.decoder, f.mat)
	if !ret {
		return ErrDecodingFailed
	}
	return nil
}

func (d *webpDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

func newWebpEncoder(decodedBy Decoder, dstBuf []byte) (*webpEncoder, error) {
	dstBuf = dstBuf[:1]
	icc := decodedBy.ICC()
	enc := C.webp_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)), unsafe.Pointer(&icc[0]), C.size_t(len(icc)))
	if enc == nil {
		return nil, ErrBufTooSmall
	}

	return &webpEncoder{
		encoder:    enc,
		dstBuf:     dstBuf,
		icc:        icc,
	}, nil
}

func (e *webpEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if f == nil {
		return nil, io.EOF
	}
	var optList []C.int
	var firstOpt *C.int
	for k, v := range opt {
		optList = append(optList, C.int(k))
		optList = append(optList, C.int(v))
	}
	if len(optList) > 0 {
		firstOpt = (*C.int)(unsafe.Pointer(&optList[0]))
	}
	length := C.webp_encoder_write(e.encoder, f.mat, firstOpt, C.size_t(len(optList)))

	if length == 0 {
		return nil, ErrInvalidImage
	}

	return e.dstBuf[:length], nil
}

func (e *webpEncoder) Close() {
	C.webp_encoder_release(e.encoder)
}
