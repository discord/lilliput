package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++11
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -ljpeg -lpng -lwebp -lippicv -lz
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/share/OpenCV/3rdparty/lib -framework Accelerate
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib
// #include "opencv.hpp"
import "C"

import (
	"errors"
	"io"
	"unsafe"
)

type ImageOrientation int

var (
	JpegQuality    = int(C.CV_IMWRITE_JPEG_QUALITY)
	PngCompression = int(C.CV_IMWRITE_PNG_COMPRESSION)
	WebpQuality    = int(C.CV_IMWRITE_WEBP_QUALITY)

	OrientationTopLeft     = ImageOrientation(C.CV_IMAGE_ORIENTATION_TL)
	OrientationTopRight    = ImageOrientation(C.CV_IMAGE_ORIENTATION_TR)
	OrientationBottomRight = ImageOrientation(C.CV_IMAGE_ORIENTATION_BR)
	OrientationBottomLeft  = ImageOrientation(C.CV_IMAGE_ORIENTATION_BL)
	OrientationLeftTop     = ImageOrientation(C.CV_IMAGE_ORIENTATION_LT)
	OrientationRightTop    = ImageOrientation(C.CV_IMAGE_ORIENTATION_RT)
	OrientationRightBottom = ImageOrientation(C.CV_IMAGE_ORIENTATION_RB)
	OrientationLeftBottom  = ImageOrientation(C.CV_IMAGE_ORIENTATION_LB)
)

type PixelType int

type ImageHeader struct {
	width       int
	height      int
	pixelType   PixelType
	orientation ImageOrientation
	numFrames   int
}

type Framebuffer struct {
	buf       []byte
	mat       C.opencv_mat
	width     int
	height    int
	pixelType PixelType
}

type OpenCVDecoder struct {
	decoder       C.opencv_decoder
	mat           C.opencv_mat
	hasReadHeader bool
	hasDecoded    bool
}

type OpenCVEncoder struct {
	encoder C.opencv_encoder
	dst     C.opencv_mat
	dstBuf  []byte
}

func (p PixelType) Depth() int {
	return int(C.opencv_type_depth(C.int(p)))
}

func (p PixelType) Channels() int {
	return int(C.opencv_type_channels(C.int(p)))
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

func (h *ImageHeader) Orientation() ImageOrientation {
	return h.orientation
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
	newMat := C.opencv_mat_create_from_data(C.int(width), C.int(height), C.int(pixelType), unsafe.Pointer(&f.buf[0]), C.size_t(len(f.buf)))
	if newMat == nil {
		return ErrBufTooSmall
	}
	f.mat = newMat
	f.width = width
	f.height = height
	f.pixelType = pixelType
	return nil
}

func (f *Framebuffer) OrientationTransform(orientation ImageOrientation) {
	if f.mat == nil {
		return
	}

	C.opencv_mat_orientation_transform(C.CVImageOrientation(orientation), f.mat)
	f.width = int(C.opencv_mat_get_width(f.mat))
	f.height = int(C.opencv_mat_get_height(f.mat))
}

func (f *Framebuffer) ResizeTo(width, height int, dst *Framebuffer) error {
	err := dst.resizeMat(width, height, f.pixelType)
	if err != nil {
		return err
	}
	C.opencv_mat_resize(f.mat, dst.mat, C.int(width), C.int(height), C.CV_INTER_AREA)
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

	newMat := C.opencv_mat_crop(f.mat, C.int(left), C.int(top), C.int(widthPostCrop), C.int(heightPostCrop))
	defer C.opencv_mat_release(newMat)

	err := dst.resizeMat(width, height, f.pixelType)
	if err != nil {
		return err
	}
	C.opencv_mat_resize(newMat, dst.mat, C.int(width), C.int(height), C.CV_INTER_AREA)
	return nil
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

func newOpenCVDecoder(buf []byte) (*OpenCVDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))

	// this next check is sort of silly since this array is 1-dimensional
	// but if the create ever changes and we goof up, could catch a
	// buffer overwrite
	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.opencv_decoder_create(mat)
	if decoder == nil {
		C.opencv_mat_release(mat)
		return nil, ErrInvalidImage
	}

	return &OpenCVDecoder{
		mat:     mat,
		decoder: decoder,
	}, nil
}

func (d *OpenCVDecoder) Header() (*ImageHeader, error) {
	if !d.hasReadHeader {
		if !C.opencv_decoder_read_header(d.decoder) {
			return nil, ErrInvalidImage
		}
	}

	d.hasReadHeader = true

	return &ImageHeader{
		width:       int(C.opencv_decoder_get_width(d.decoder)),
		height:      int(C.opencv_decoder_get_height(d.decoder)),
		pixelType:   PixelType(C.opencv_decoder_get_pixel_type(d.decoder)),
		orientation: ImageOrientation(C.opencv_decoder_get_orientation(d.decoder)),
		numFrames:   1,
	}, nil
}

func (d *OpenCVDecoder) Close() {
	C.opencv_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
}

func (d *OpenCVDecoder) Description() string {
	return C.GoString(C.opencv_decoder_get_description(d.decoder))
}

func (d *OpenCVDecoder) DecodeTo(f *Framebuffer) error {
	if d.hasDecoded {
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
	ret := C.opencv_decoder_read_data(d.decoder, f.mat)
	if !ret {
		return ErrDecodingFailed
	}
	d.hasDecoded = true
	return nil
}

func newOpenCVEncoder(ext string, decodedBy Decoder, dstBuf []byte) (*OpenCVEncoder, error) {
	dstBuf = dstBuf[:1]
	dst := C.opencv_mat_create_empty_from_data(C.int(cap(dstBuf)), unsafe.Pointer(&dstBuf[0]))

	if dst == nil {
		return nil, ErrBufTooSmall
	}

	c_ext := C.CString(ext)
	defer C.free(unsafe.Pointer(c_ext))
	enc := C.opencv_encoder_create(c_ext, dst)
	if enc == nil {
		return nil, ErrInvalidImage
	}

	return &OpenCVEncoder{
		encoder: enc,
		dst:     dst,
		dstBuf:  dstBuf,
	}, nil
}

func (e *OpenCVEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
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
	if !C.opencv_encoder_write(e.encoder, f.mat, firstOpt, C.size_t(len(optList))) {
		return nil, ErrInvalidImage
	}

	ptrCheck := C.opencv_mat_get_data(e.dst)
	if ptrCheck != unsafe.Pointer(&e.dstBuf[0]) {
		// mat pointer got reallocated - the passed buf was too small to hold the image
		// XXX we should free? the mat here, probably want to recreate
		return nil, ErrBufTooSmall
	}

	length := int(C.opencv_mat_get_height(e.dst))

	return e.dstBuf[:length], nil
}

func (e *OpenCVEncoder) Close() {
	C.opencv_encoder_release(e.encoder)
	C.opencv_mat_release(e.dst)
}
