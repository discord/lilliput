package lilliput

// #cgo CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx
// #cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo CXXFLAGS: -std=c++11
// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -ljpeg -lpng -lwebp -lsharpyuv -lippicv -lz
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/share/OpenCV/3rdparty/lib
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib
// #include "opencv.hpp"
import "C"

import (
	"bytes"
	"encoding/binary"
	"errors"
	"io"
	"time"
	"unsafe"
)

// ImageOrientation describes how the decoded image is oriented according to its metadata.
type ImageOrientation int

const (
	JpegQuality    = int(C.CV_IMWRITE_JPEG_QUALITY)
	PngCompression = int(C.CV_IMWRITE_PNG_COMPRESSION)
	WebpQuality    = int(C.CV_IMWRITE_WEBP_QUALITY)

	JpegProgressive = int(C.CV_IMWRITE_JPEG_PROGRESSIVE)

	OrientationTopLeft     = ImageOrientation(C.CV_IMAGE_ORIENTATION_TL)
	OrientationTopRight    = ImageOrientation(C.CV_IMAGE_ORIENTATION_TR)
	OrientationBottomRight = ImageOrientation(C.CV_IMAGE_ORIENTATION_BR)
	OrientationBottomLeft  = ImageOrientation(C.CV_IMAGE_ORIENTATION_BL)
	OrientationLeftTop     = ImageOrientation(C.CV_IMAGE_ORIENTATION_LT)
	OrientationRightTop    = ImageOrientation(C.CV_IMAGE_ORIENTATION_RT)
	OrientationRightBottom = ImageOrientation(C.CV_IMAGE_ORIENTATION_RB)
	OrientationLeftBottom  = ImageOrientation(C.CV_IMAGE_ORIENTATION_LB)

	pngChunkSizeFieldLen = 4
	pngChunkTypeFieldLen = 4
	pngChunkAllFieldsLen = 12

	jpegEOISegmentType byte = 0xD9
	jpegSOSSegmentType byte = 0xDA
)

var (
	pngActlChunkType = []byte{byte('a'), byte('c'), byte('T'), byte('L')}
	pngFctlChunkType = []byte{byte('f'), byte('c'), byte('T'), byte('L')}
	pngFdatChunkType = []byte{byte('f'), byte('d'), byte('A'), byte('T')}
	pngIendChunkType = []byte{byte('I'), byte('E'), byte('N'), byte('D')}

	// Helpful: https://en.wikipedia.org/wiki/JPEG#Syntax_and_structure
	jpegUnsizedSegmentTypes = map[byte]bool{
		0xD0:               true, // RST segments
		0xD1:               true,
		0xD2:               true,
		0xD3:               true,
		0xD4:               true,
		0xD5:               true,
		0xD6:               true,
		0xD7:               true, // end RST segments
		0xD8:               true, // SOI
		jpegEOISegmentType: true,
	}
)

// PixelType describes the base pixel type of the image.
type PixelType int

// ImageHeader contains basic decoded image metadata.
type ImageHeader struct {
	width         int
	height        int
	pixelType     PixelType
	orientation   ImageOrientation
	numFrames     int
	contentLength int
}

// Framebuffer contains an array of raw, decoded pixel data.
type Framebuffer struct {
	buf       []byte
	mat       C.opencv_mat
	width     int
	height    int
	pixelType PixelType
	duration  time.Duration
}

type openCVDecoder struct {
	decoder       C.opencv_decoder
	mat           C.opencv_mat
	buf           []byte
	hasReadHeader bool
	hasDecoded    bool
}

type openCVEncoder struct {
	encoder C.opencv_encoder
	dst     C.opencv_mat
	dstBuf  []byte
}

// Depth returns the number of bits in the PixelType.
func (p PixelType) Depth() int {
	return int(C.opencv_type_depth(C.int(p)))
}

// Channels returns the number of channels in the PixelType.
func (p PixelType) Channels() int {
	return int(C.opencv_type_channels(C.int(p)))
}

// Width returns the width of the image in number of pixels.
func (h *ImageHeader) Width() int {
	return h.width
}

// Height returns the height of the image in number of pixels.
func (h *ImageHeader) Height() int {
	return h.height
}

// PixelType returns a PixelType describing the image's pixels.
func (h *ImageHeader) PixelType() PixelType {
	return h.pixelType
}

// ImageOrientation returns the metadata-based image orientation.
func (h *ImageHeader) Orientation() ImageOrientation {
	return h.orientation
}

func (h *ImageHeader) IsAnimated() bool {
	return h.numFrames > 1
}

// Some images have extra padding bytes at the end that aren't needed.
// In the worst case, this might be unwanted data that the user intended
// to crop (e.g. "acropalypse" bug).
// This function returns the length of the necessary image data. Data
// past this point can be safely truncated `data[:h.ContentLength()]`
func (h *ImageHeader) ContentLength() int {
	return h.contentLength
}

// NewFramebuffer creates the backing store for a pixel frame buffer.
func NewFramebuffer(width, height int) *Framebuffer {
	return &Framebuffer{
		buf: make([]byte, width*height*4),
		mat: nil,
	}
}

// Close releases the resources associated with Framebuffer.
func (f *Framebuffer) Close() {
	if f.mat != nil {
		C.opencv_mat_release(f.mat)
		f.mat = nil
	}
}

// Clear resets all of the pixel data in Framebuffer.
func (f *Framebuffer) Clear() {
	C.memset(unsafe.Pointer(&f.buf[0]), 0, C.size_t(len(f.buf)))
}

func (f *Framebuffer) resizeMat(width, height int, pixelType PixelType) error {
	if f.mat != nil {
		C.opencv_mat_release(f.mat)
		f.mat = nil
	}
	if pixelType.Depth() > 8 {
		pixelType = PixelType(C.opencv_type_convert_depth(C.int(pixelType), C.CV_8U))
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

// OrientationTransform rotates and/or mirrors the Framebuffer. Passing the
// orientation given by the ImageHeader will normalize the orientation of the Framebuffer.
func (f *Framebuffer) OrientationTransform(orientation ImageOrientation) {
	if f.mat == nil {
		return
	}

	C.opencv_mat_orientation_transform(C.CVImageOrientation(orientation), f.mat)
	f.width = int(C.opencv_mat_get_width(f.mat))
	f.height = int(C.opencv_mat_get_height(f.mat))
}

// ResizeTo performs a resizing transform on the Framebuffer and puts the result
// in the provided destination Framebuffer. This function does not preserve aspect
// ratio if the given dimensions differ in ratio from the source. Returns an error
// if the destination is not large enough to hold the given dimensions.
func (f *Framebuffer) ResizeTo(width, height int, dst *Framebuffer) error {
	if width < 1 {
		width = 1
	}

	if height < 1 {
		height = 1
	}

	err := dst.resizeMat(width, height, f.pixelType)
	if err != nil {
		return err
	}
	C.opencv_mat_resize(f.mat, dst.mat, C.int(width), C.int(height), C.CV_INTER_AREA)
	return nil
}

// Fit performs a resizing and cropping transform on the Framebuffer and puts the result
// in the provided destination Framebuffer. This function does preserve aspect ratio
// but will crop columns or rows from the edges of the image as necessary in order to
// keep from stretching the image content. Returns an error if the destination is
// not large enough to hold the given dimensions.
func (f *Framebuffer) Fit(width, height int, dst *Framebuffer) error {
	if f.mat == nil {
		return ErrFrameBufNoPixels
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

	if widthPostCrop < 1 {
		widthPostCrop = 1
	}

	if heightPostCrop < 1 {
		heightPostCrop = 1
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

// Width returns the width of the contained pixel data in number of pixels. This may
// differ from the capacity of the framebuffer.
func (f *Framebuffer) Width() int {
	return f.width
}

// Height returns the height of the contained pixel data in number of pixels. This may
// differ from the capacity of the framebuffer.
func (f *Framebuffer) Height() int {
	return f.height
}

// PixelType returns the PixelType information of the contained pixel data, if any.
func (f *Framebuffer) PixelType() PixelType {
	return f.pixelType
}

// Duration returns the length of time this frame plays out in an animated image
func (f *Framebuffer) Duration() time.Duration {
	return f.duration
}

func newOpenCVDecoder(buf []byte) (*openCVDecoder, error) {
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

	return &openCVDecoder{
		mat:     mat,
		decoder: decoder,
		buf:     buf,
	}, nil
}

// chunk format https://www.w3.org/TR/PNG-Structure.html
// TLDR: 4 bytes length, 4 bytes type, variable data, 4 bytes CRC
// length is only the "data" field; does not include itself, the type or the CRC
type pngChunkIter struct {
	png        []byte
	iterOffset int
}

func makePngChunkIter(png []byte) (*pngChunkIter, error) {
	if !bytes.HasPrefix(png, pngMagic) {
		return nil, errors.New("Image is not PNG")
	}

	return &pngChunkIter{
		png: png, iterOffset: 0,
	}, nil
}

func (it *pngChunkIter) hasSpaceForChunk() bool {
	return it.iterOffset+pngChunkAllFieldsLen <= len(it.png)
}

// byte offset of the next chunk. might be past the end of the data
// for the last chunk, or if the chunk is malformed
func (it *pngChunkIter) nextChunkOffset() int {
	chunkDataSize := (int)(binary.BigEndian.Uint32(it.png[it.iterOffset:]))
	return it.iterOffset + chunkDataSize + pngChunkAllFieldsLen
}

func (it *pngChunkIter) next() bool {
	if it.iterOffset < len(pngMagic) {
		// move to the first chunk by skipping png magic prefix
		it.iterOffset = len(pngMagic)
		return it.hasSpaceForChunk()
	}
	if !it.hasSpaceForChunk() {
		return false
	}

	it.iterOffset = it.nextChunkOffset()
	return it.hasSpaceForChunk()
}

func (it *pngChunkIter) chunkType() []byte {
	return it.png[it.iterOffset+4 : it.iterOffset+8]
}

func detectContentLengthPNG(png []byte) int {
	chunkIter, err := makePngChunkIter(png)
	if err != nil {
		// This is not a png, take all the data
		return len(png)
	}

	for chunkIter.next() {
		chunkType := chunkIter.chunkType()
		if bytes.Equal(chunkType, pngIendChunkType) {
			eofOffset := chunkIter.nextChunkOffset()
			if eofOffset > len(png) {
				eofOffset = len(png)
			}
			return eofOffset
		}
	}
	// Didn't find IEND. File is malformed but let's continue anyway
	return len(png)
}

func detectContentLengthJPEG(jpeg []byte) int {
	// check if this is maybe jpeg
	jpegPrefix := []byte{0xFF, 0xD8, 0xFF}
	if !bytes.HasPrefix(jpeg, jpegPrefix) {
		// Not jpeg if it doesn't begin with SOI
		return len(jpeg)
	}

	// Iterate through jpeg segments
	idx := 0
	for {
		if idx+1 >= len(jpeg) {
			break
		}
		if jpeg[idx] != 0xFF {
			// not valid jpeg
			break
		}

		// Segments are at least 2 bytes big
		nextSegmentStart := idx + 2

		// find current segment type
		segmentType := jpeg[idx+1]
		if segmentType == jpegEOISegmentType {
			// EOI means the end of image content
			return nextSegmentStart
		} else if segmentType == 0xFF {
			// Some handling for padding
			idx++
			continue
		}

		if _, isUnsized := jpegUnsizedSegmentTypes[segmentType]; isUnsized {
			idx = nextSegmentStart
			continue
		}

		if idx+3 >= len(jpeg) {
			// not enough data to continue
			break
		}
		// 2 bytes size includes itself
		nextSegmentStart += (int)(binary.BigEndian.Uint16(jpeg[idx+2:]))

		if segmentType == jpegSOSSegmentType {
			// start of scan means that ECS data follows
			// ECS data does not start with 0xFF marker
			// scan through ECS to find next segment which starts with 0xFF
			for ; nextSegmentStart < len(jpeg); nextSegmentStart++ {
				if jpeg[nextSegmentStart] != 0xFF {
					continue
				}

				if nextSegmentStart+1 >= len(jpeg) {
					nextSegmentStart = len(jpeg)
					break
				}
				peek := jpeg[nextSegmentStart+1]
				if peek == 0xFF {
					// there can be padding bytes which are repeated 0xFF
					continue
				}
				// 0 means this is a raw 0xFF in the ECS data
				// RST segment types are also a continuation of ECS data
				if peek != 0 && (peek < 0xD0 || peek > 0xD7) {
					// Reached the end of ECS!
					break
				}
			}
		}
		idx = nextSegmentStart
	}

	// if we didn't find EOI, fallback to the full length
	return len(jpeg)
}

func detectContentLength(img []byte) int {
	// both of these short circuit if the correct prefix isn't detected
	// so we can just call both with little cost for simpler code
	jpegLength := detectContentLengthJPEG(img)
	pngLength := detectContentLengthPNG(img)
	if jpegLength < pngLength {
		return jpegLength
	}
	return pngLength
}

// detectAPNG detects if a blob contains a PNG with animated segments
func detectAPNG(maybeAPNG []byte) bool {
	chunkIter, err := makePngChunkIter(maybeAPNG)
	if err != nil {
		// This is not a png at all :)
		return false
	}

	for chunkIter.next() {
		chunkType := chunkIter.chunkType()
		if bytes.Equal(chunkType, pngActlChunkType) || bytes.Equal(chunkType, pngFctlChunkType) || bytes.Equal(chunkType, pngFdatChunkType) {
			return true
		}
	}
	return false
}

func (d *openCVDecoder) Header() (*ImageHeader, error) {
	if !d.hasReadHeader {
		if !C.opencv_decoder_read_header(d.decoder) {
			return nil, ErrInvalidImage
		}
	}

	d.hasReadHeader = true

	numFrames := 1
	if detectAPNG(d.buf) {
		numFrames = 2
	}

	return &ImageHeader{
		width:         int(C.opencv_decoder_get_width(d.decoder)),
		height:        int(C.opencv_decoder_get_height(d.decoder)),
		pixelType:     PixelType(C.opencv_decoder_get_pixel_type(d.decoder)),
		orientation:   ImageOrientation(C.opencv_decoder_get_orientation(d.decoder)),
		numFrames:     numFrames,
		contentLength: detectContentLength(d.buf),
	}, nil
}

func (d *openCVDecoder) Close() {
	C.opencv_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

func (d *openCVDecoder) Description() string {
	return C.GoString(C.opencv_decoder_get_description(d.decoder))
}

func (d *openCVDecoder) IsStreamable() bool {
	return true
}

func (d *openCVDecoder) Duration() time.Duration {
	return time.Duration(0)
}

func (d *openCVDecoder) DecodeTo(f *Framebuffer) error {
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

func (d *openCVDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

func newOpenCVEncoder(ext string, decodedBy Decoder, dstBuf []byte) (*openCVEncoder, error) {
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

	return &openCVEncoder{
		encoder: enc,
		dst:     dst,
		dstBuf:  dstBuf,
	}, nil
}

func (e *openCVEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
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

func (e *openCVEncoder) Close() {
	C.opencv_encoder_release(e.encoder)
	C.opencv_mat_release(e.dst)
}
