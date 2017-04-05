package opencv

// #include "opencv.hpp"
// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo linux pkg-config: opencv
// #cgo darwin pkg-config: opencv
import "C"

import (
	"errors"
	"unsafe"
)

var (
	JpegQuality    = int(C.CV_IMWRITE_JPEG_QUALITY)
	PngCompression = int(C.CV_IMWRITE_PNG_COMPRESSION)
	WebpQuality    = int(C.CV_IMWRITE_WEBP_QUALITY)

	ErrInvalidImage   = errors.New("unrecognized image format")
	ErrDecodingFailed = errors.New("failed to decode image")
	ErrBufTooSmall    = errors.New("buffer too small to hold pixel frame")
)

type PixelType int

func (p PixelType) Depth() int {
	return int(C.opencv_type_depth(C.int(p)))
}

func (p PixelType) Channels() int {
	return int(C.opencv_type_channels(C.int(p)))
}

type Framebuffer struct {
	buf       []byte
	mat       C.opencv_Mat
	width     int
	height    int
	pixelType PixelType
}

type ImageHeader struct {
	width     int
	height    int
	pixelType PixelType
}

type Decoder struct {
	decoder       C.opencv_Decoder
	hasReadHeader bool
}

type Encoder struct {
	encoder C.opencv_Encoder
	vec     C.vec
	buf     []byte
}

func (h *ImageHeader) Width() int {
	return h.width
}

func (h *ImageHeader) Height() int {
	return h.height
}

func (h *ImageHeader) PixelType() PixelType {
	return h.pixelType
}

func NewDecoder(buf []byte) (*Decoder, error) {
	encoded := C.opencv_createMatFromData(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	// this next check is sort of silly since this array is 1-dimensional
	// but if the create ever changes and we goof up, could catch a
	// buffer overwrite
	if encoded == nil {
		return nil, ErrBufTooSmall
	}
	defer C.opencv_mat_release(encoded)

	decoder := C.opencv_createDecoder(encoded)
	if decoder == nil {
		return nil, ErrInvalidImage
	}

	if !C.opencv_decoder_set_source(decoder, encoded) {
		C.opencv_decoder_release(decoder)
		return nil, ErrInvalidImage
	}

	return &Decoder{
		decoder: decoder,
	}, nil
}

func (d *Decoder) Header() (*ImageHeader, error) {
	if !d.hasReadHeader {
		if !C.opencv_decoder_read_header(d.decoder) {
			return nil, ErrInvalidImage
		}
	}

	d.hasReadHeader = true

	return &ImageHeader{
		width:     int(C.opencv_decoder_get_width(d.decoder)),
		height:    int(C.opencv_decoder_get_height(d.decoder)),
		pixelType: PixelType(C.opencv_decoder_get_pixel_type(d.decoder)),
	}, nil
}

func (d *Decoder) Close() {
	C.opencv_decoder_release(d.decoder)
}

func (d *Decoder) Description() string {
	return C.GoString(C.opencv_decoder_get_description(d.decoder))
}

func (d *Decoder) DecodeTo(f *Framebuffer) error {
	h, err := d.Header()
	if err != nil {
		return err
	}
	err = f.resizeMat(h.Width(), h.Height(), h.PixelType())
	if err != nil {
		return err
	}
	ret := C.opencv_decoder_read_data(d.decoder, f.mat)
	if !ret {
		return ErrDecodingFailed
	}
	return nil
}

func NewEncoder(ext string) (*Encoder, error) {
	enc := C.opencv_createEncoder(C.CString(ext))
	if enc == nil {
		return nil, ErrInvalidImage
	}

	vec := C.vec_create()

	if !C.opencv_encoder_set_destination(enc, vec) {
		C.opencv_encoder_release(enc)
		C.vec_destroy(vec)
		return nil, ErrInvalidImage
	}

	return &Encoder{
		encoder: enc,
		vec:     vec,
	}, nil
}

func (e *Encoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	var optList []C.int
	for k, v := range opt {
		optList = append(optList, C.int(k))
		optList = append(optList, C.int(v))
	}
	if !C.opencv_encoder_write(e.encoder, f.mat, (*C.int)(unsafe.Pointer(&optList[0])), C.size_t(len(optList))) {
		return nil, ErrInvalidImage
	}
	vec_len := int(C.vec_size(e.vec))
	dst := make([]byte, vec_len)
	copied := int(C.vec_copy(e.vec, unsafe.Pointer(&dst[0]), C.size_t(len(dst))))
	if copied != vec_len {
		return nil, ErrBufTooSmall
	}
	return dst, nil
}

func (e *Encoder) Close() {
	C.opencv_encoder_release(e.encoder)
	C.vec_destroy(e.vec)
}

// Allocate the backing store for a pixel frame buffer
// Make it big enough to hold width*height, 4 channels, 8 bits per pixel
func NewFramebuffer(width, height int) *Framebuffer {
	return &Framebuffer{
		buf: make([]byte, width*height*4),
		mat: nil,
	}
}

func (f *Framebuffer) Close() {
	if f.mat != nil {
		C.opencv_mat_release(f.mat)
		f.mat = nil
	}
}

func (f *Framebuffer) Clear() {
	C.memset(unsafe.Pointer(&f.buf[0]), 0, C.size_t(len(f.buf)))
}

func (f *Framebuffer) resizeMat(width, height int, pixelType PixelType) error {
	if f.mat != nil {
		C.opencv_mat_release(f.mat)
		f.mat = nil
	}
	newMat := C.opencv_createMatFromData(C.int(width), C.int(height), C.int(pixelType), unsafe.Pointer(&f.buf[0]), C.size_t(len(f.buf)))
	if newMat == nil {
		return ErrBufTooSmall
	}
	f.mat = newMat
	f.width = width
	f.height = height
	f.pixelType = pixelType
	return nil
}

func (f *Framebuffer) ResizeTo(width, height int, dst *Framebuffer) error {
	err := dst.resizeMat(width, height, f.pixelType)
	if err != nil {
		return err
	}
	C.opencv_resize(f.mat, dst.mat, C.int(width), C.int(height), C.CV_INTER_CUBIC)
	return nil
}

// Fit operator is taken from PIL's fit()
func (f *Framebuffer) Fit(width, height int, dst *Framebuffer) error {
	if f.mat == nil {
		return errors.New("Framebuffer contains no pixels")
	}

	aspectIn := float64(f.width) / float64(f.height)
	aspectOut := float64(width) / float64(height)

	var widthPostCrop, heightPostCrop int
	if aspectIn > aspectOut {
		// input is wider than output, so we'll need to narrow
		// we preserve input height and reduce width
		widthPostCrop = int((aspectOut * float64(f.height)) + 0.5)
		heightPostCrop = f.height
	} else {
		// input is taller than output, so we'll need to shrink
		heightPostCrop = int((float64(f.width) / aspectOut) + 0.5)
		widthPostCrop = f.width
	}

	var left, top int
	left = int(float64(f.width-widthPostCrop) * 0.5)
	if left < 0 {
		left = 0
	}

	top = int(float64(f.height-heightPostCrop) * 0.5)
	if top < 0 {
		top = 0
	}

	newMat := C.opencv_crop(f.mat, C.int(left), C.int(top), C.int(widthPostCrop), C.int(heightPostCrop))
	C.opencv_mat_release(f.mat)
	f.mat = newMat

	return f.ResizeTo(width, height, dst)
}

func (f *Framebuffer) Width() int {
	return f.width
}

func (f *Framebuffer) Height() int {
	return f.height
}

func (f *Framebuffer) PixelType() PixelType {
	return f.pixelType
}
