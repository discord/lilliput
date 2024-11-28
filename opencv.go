package lilliput

// #include "opencv.hpp"
import "C"

import (
	"bytes"
	"encoding/binary"
	"errors"
	"image"
	"io"
	"time"
	"unsafe"
)

// DisposeMethod describes how the previous frame should be disposed before rendering the next frame.
type DisposeMethod int

const (
	// NoDispose indicates the previous frame should remain as-is
	NoDispose DisposeMethod = iota
	// DisposeToBackgroundColor indicates the previous frame area should be cleared to background color
	DisposeToBackgroundColor
)

// BlendMethod describes how the previous frame should be blended with the next frame.
type BlendMethod int

const (
	// UseAlphaBlending indicates alpha blending should be used when compositing frames
	UseAlphaBlending BlendMethod = iota
	// NoBlend indicates frames should be copied directly without blending
	NoBlend
)

// ImageOrientation describes how the decoded image is oriented according to its metadata.
type ImageOrientation int

const (
	// Standard image encoding constants
	JpegQuality     = int(C.CV_IMWRITE_JPEG_QUALITY)     // Quality parameter for JPEG encoding (0-100)
	PngCompression  = int(C.CV_IMWRITE_PNG_COMPRESSION)  // Compression level for PNG encoding (0-9)
	WebpQuality     = int(C.CV_IMWRITE_WEBP_QUALITY)     // Quality parameter for WebP encoding (0-100)
	JpegProgressive = int(C.CV_IMWRITE_JPEG_PROGRESSIVE) // Enable progressive JPEG encoding

	// Image orientation constants
	OrientationTopLeft     = ImageOrientation(C.CV_IMAGE_ORIENTATION_TL)
	OrientationTopRight    = ImageOrientation(C.CV_IMAGE_ORIENTATION_TR)
	OrientationBottomRight = ImageOrientation(C.CV_IMAGE_ORIENTATION_BR)
	OrientationBottomLeft  = ImageOrientation(C.CV_IMAGE_ORIENTATION_BL)
	OrientationLeftTop     = ImageOrientation(C.CV_IMAGE_ORIENTATION_LT)
	OrientationRightTop    = ImageOrientation(C.CV_IMAGE_ORIENTATION_RT)
	OrientationRightBottom = ImageOrientation(C.CV_IMAGE_ORIENTATION_RB)
	OrientationLeftBottom  = ImageOrientation(C.CV_IMAGE_ORIENTATION_LB)

	// PNG chunk field lengths
	pngChunkSizeFieldLen = 4
	pngChunkTypeFieldLen = 4
	pngChunkAllFieldsLen = 12

	// JPEG segment type markers
	jpegEOISegmentType byte = 0xD9 // End of Image marker
	jpegSOSSegmentType byte = 0xDA // Start of Scan marker
)

// PNG chunk type identifiers
var (
	pngActlChunkType = []byte{byte('a'), byte('c'), byte('T'), byte('L')} // Animation Control Chunk
	pngFctlChunkType = []byte{byte('f'), byte('c'), byte('T'), byte('L')} // Frame Control Chunk
	pngFdatChunkType = []byte{byte('f'), byte('d'), byte('A'), byte('T')} // Frame Data Chunk
	pngIendChunkType = []byte{byte('I'), byte('E'), byte('N'), byte('D')} // Image End Chunk

	// Map of JPEG segment types that don't have a size field
	jpegUnsizedSegmentTypes = map[byte]bool{
		0xD0:               true, // RST0 marker
		0xD1:               true, // RST1 marker
		0xD2:               true, // RST2 marker
		0xD3:               true, // RST3 marker
		0xD4:               true, // RST4 marker
		0xD5:               true, // RST5 marker
		0xD6:               true, // RST6 marker
		0xD7:               true, // RST7 marker
		0xD8:               true, // SOI marker
		jpegEOISegmentType: true, // EOI marker
	}
)

// PixelType describes the base pixel type of the image.
type PixelType int

// ImageHeader contains basic decoded image metadata.
type ImageHeader struct {
	width         int              // Width of the image in pixels
	height        int              // Height of the image in pixels
	pixelType     PixelType        // Type of pixels in the image
	orientation   ImageOrientation // Orientation from image metadata
	numFrames     int              // Number of frames (1 for static images)
	contentLength int              // Length of actual image content
}

// Framebuffer contains an array of raw, decoded pixel data.
type Framebuffer struct {
	buf       []byte        // Raw pixel data
	mat       C.opencv_mat  // OpenCV matrix containing the pixel data
	width     int           // Width of the frame in pixels
	height    int           // Height of the frame in pixels
	pixelType PixelType     // Type of pixels in the frame
	duration  time.Duration // Duration to display this frame
	xOffset   int           // X offset for drawing this frame
	yOffset   int           // Y offset for drawing this frame
	dispose   DisposeMethod // How to dispose previous frame
	blend     BlendMethod   // How to blend with previous frame
}

// openCVDecoder implements the Decoder interface for images supported by OpenCV.
type openCVDecoder struct {
	decoder       C.opencv_decoder // Native OpenCV decoder
	mat           C.opencv_mat     // OpenCV matrix containing the image data
	buf           []byte           // Original encoded image data
	hasReadHeader bool             // Whether header has been read
	hasDecoded    bool             // Whether image has been decoded
}

// openCVEncoder implements the Encoder interface for images supported by OpenCV.
type openCVEncoder struct {
	encoder C.opencv_encoder // Native OpenCV encoder
	dst     C.opencv_mat     // Destination OpenCV matrix
	dstBuf  []byte           // Destination buffer for encoded data
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

// Orientation returns the metadata-based image orientation.
func (h *ImageHeader) Orientation() ImageOrientation {
	return h.orientation
}

// IsAnimated returns true if the image contains multiple frames.
func (h *ImageHeader) IsAnimated() bool {
	return h.numFrames > 1
}

// HasAlpha returns true if the image has an alpha channel.
func (h *ImageHeader) HasAlpha() bool {
	return h.pixelType.Channels() == 4
}

// ContentLength returns the length of the necessary image data.
// Data past this point can be safely truncated using data[:h.ContentLength()].
// This helps handle padding bytes and potential unwanted trailing data.
// This could be applicable to images with unwanted data at the end (e.g. "acropalypse" bug).
func (h *ImageHeader) ContentLength() int {
	return h.contentLength
}

// NewFramebuffer creates a backing store for a pixel frame buffer with the specified dimensions.
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

// Clear resets all pixel data in Framebuffer for the active frame and resets the mat if it exists.
func (f *Framebuffer) Clear() {
	C.memset(unsafe.Pointer(&f.buf[0]), 0, C.size_t(len(f.buf)))
	if f.mat != nil {
		C.opencv_mat_reset(f.mat)
	}
}

// Create3Channel initializes the framebuffer for 3-channel (RGB) image data.
func (f *Framebuffer) Create3Channel(width, height int) error {
	if err := f.resizeMat(width, height, C.CV_8UC3); err != nil {
		return err
	}
	f.Clear()
	return nil
}

// Create4Channel initializes the framebuffer for 4-channel (RGBA) image data.
func (f *Framebuffer) Create4Channel(width, height int) error {
	if err := f.resizeMat(width, height, C.CV_8UC4); err != nil {
		return err
	}
	f.Clear()
	return nil
}

// resizeMat resizes the OpenCV matrix to the specified dimensions and pixel type.
// Returns ErrBufTooSmall if the matrix cannot be created at the specified size.
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

// OrientationTransform rotates and/or mirrors the Framebuffer according to the given orientation.
// Passing the orientation from ImageHeader will normalize the orientation.
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

// ClearToTransparent clears a rectangular region of the framebuffer to transparent.
func (f *Framebuffer) ClearToTransparent(rect image.Rectangle) error {
	if f.mat == nil {
		return errors.New("framebuffer matrix is nil")
	}

	result := C.opencv_mat_clear_to_transparent(f.mat, C.int(rect.Min.X), C.int(rect.Min.Y), C.int(rect.Dx()), C.int(rect.Dy()))
	return handleOpenCVError(result)
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

// handleOpenCVError converts an OpenCV error code to an error
func handleOpenCVError(result C.int) error {
	switch result {
	case C.OPENCV_SUCCESS:
		return nil
	case C.OPENCV_ERROR_INVALID_CHANNEL_COUNT:
		return errors.New("error copying opencv data: source image must have 3 or 4 channels")
	case C.OPENCV_ERROR_OUT_OF_BOUNDS:
		return errors.New("error copying opencv data: source image with offsets exceeds the bounds of the destination framebuffer")
	case C.OPENCV_ERROR_NULL_MATRIX:
		return errors.New("error copying opencv data: source or destination matrix is null")
	case C.OPENCV_ERROR_ALPHA_BLENDING_FAILED:
		return errors.New("error copying opencv data: alpha blending failed")
	case C.OPENCV_ERROR_FINAL_CONVERSION_FAILED:
		return errors.New("error copying opencv data: final conversion failed")
	case C.OPENCV_ERROR_CONVERSION_FAILED:
		return errors.New("error copying opencv data: conversion failed")
	case C.OPENCV_ERROR_RESIZE_FAILED:
		return errors.New("error copying opencv data: resize failed")
	case C.OPENCV_ERROR_COPY_FAILED:
		return errors.New("error copying opencv data: copy failed")
	case C.OPENCV_ERROR_INVALID_DIMENSIONS:
		return errors.New("error copying opencv data: invalid dimensions")
	case C.OPENCV_ERROR_UNKNOWN:
		return errors.New("unknown error copying opencv data")
	default:
		return errors.New("unknown error occurred during alpha blending")
	}
}

// CopyToOffsetWithAlphaBlending copies the source framebuffer to a specified rectangle within the destination framebuffer.
// This function performs alpha blending.
func (f *Framebuffer) CopyToOffsetWithAlphaBlending(src *Framebuffer, rect image.Rectangle) error {
	result := C.opencv_copy_to_region_with_alpha(src.mat, f.mat, C.int(rect.Min.X), C.int(rect.Min.Y), C.int(rect.Dx()), C.int(rect.Dy()))
	return handleOpenCVError(result)
}

// CopyToOffsetNoBlend copies the source framebuffer to a specified rectangle within the destination framebuffer.
// This function does not perform any blending.
func (f *Framebuffer) CopyToOffsetNoBlend(src *Framebuffer, rect image.Rectangle) error {
	result := C.opencv_copy_to_region(src.mat, f.mat, C.int(rect.Min.X), C.int(rect.Min.Y), C.int(rect.Dx()), C.int(rect.Dy()))
	return handleOpenCVError(result)
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

func (d *openCVDecoder) BackgroundColor() uint32 {
	return 0xFFFFFFFF
}

func (d *openCVDecoder) LoopCount() int {
	return 0 // loop indefinitely
}

func (d *openCVDecoder) HasSubtitles() bool {
	return false
}

func (d *openCVDecoder) ICC() []byte {
	switch d.Description() {
	case "JPEG":
		return d.iccJPEG()
	case "PNG":
		return d.iccPNG()
	}
	return []byte{}
}

func (d *openCVDecoder) iccJPEG() []byte {
	iccDst := make([]byte, 8192)
	iccLength := C.opencv_decoder_get_jpeg_icc(unsafe.Pointer(&d.buf[0]), C.size_t(len(d.buf)), unsafe.Pointer(&iccDst[0]), C.size_t(cap(iccDst)))
	return iccDst[:iccLength]
}

func (d *openCVDecoder) iccPNG() []byte {
	iccDst := make([]byte, 8192)
	iccLength := C.opencv_decoder_get_png_icc(unsafe.Pointer(&d.buf[0]), C.size_t(len(d.buf)), unsafe.Pointer(&iccDst[0]), C.size_t(cap(iccDst)))
	return iccDst[:iccLength]
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
	f.blend = NoBlend
	f.dispose = DisposeToBackgroundColor
	f.xOffset = 0
	f.yOffset = 0
	f.duration = time.Duration(0)
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
