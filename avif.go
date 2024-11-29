package lilliput

// #include "avif.hpp"
import "C"

import (
	"io"
	"time"
	"unsafe"
)

// Types
// ----------------------------------------

type avifDecoder struct {
	decoder C.avif_decoder
	mat     C.opencv_mat
	buf     []byte
}

type avifEncoder struct {
	encoder    C.avif_encoder
	dstBuf     []byte
	icc        []byte
	bgColor    uint32
	frameIndex int
	hasFlushed bool
}

// Decoder Implementation
// ----------------------------------------

func newAvifDecoder(buf []byte) (*avifDecoder, error) {
	mat := C.opencv_mat_create_from_data(C.int(len(buf)), 1, C.CV_8U, unsafe.Pointer(&buf[0]), C.size_t(len(buf)))
	if mat == nil {
		return nil, ErrBufTooSmall
	}

	decoder := C.avif_decoder_create(mat)
	if decoder == nil {
		return nil, ErrInvalidImage
	}

	return &avifDecoder{
		decoder: decoder,
		mat:     mat,
		buf:     buf,
	}, nil
}

func (d *avifDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width:         int(C.avif_decoder_get_width(d.decoder)),
		height:        int(C.avif_decoder_get_height(d.decoder)),
		pixelType:     PixelType(C.avif_decoder_get_pixel_type(d.decoder)),
		orientation:   OrientationTopLeft,
		numFrames:     int(C.avif_decoder_get_num_frames(d.decoder)),
		contentLength: len(d.buf),
	}, nil
}

func (d *avifDecoder) DecodeTo(f *Framebuffer) error {
	if f == nil {
		return io.EOF
	}

	if d.hasReachedEndOfFrames() {
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

	ret := C.avif_decoder_decode(d.decoder, f.mat)
	if !ret {
		return ErrDecodingFailed
	}

	// Set frame properties
	f.duration = time.Duration(C.avif_decoder_get_frame_duration(d.decoder)) * time.Millisecond
	f.dispose = DisposeMethod(C.avif_decoder_get_frame_dispose(d.decoder))
	f.blend = BlendMethod(C.avif_decoder_get_frame_blend(d.decoder))
	f.xOffset = int(C.avif_decoder_get_frame_x_offset(d.decoder))
	f.yOffset = int(C.avif_decoder_get_frame_y_offset(d.decoder))

	return nil
}

// Decoder Helper Methods
// ----------------------------------------

func (d *avifDecoder) hasReachedEndOfFrames() bool {
	return C.avif_decoder_has_more_frames(d.decoder) == 0
}

func (d *avifDecoder) ICC() []byte {
	iccDst := make([]byte, 8192)
	iccLength := C.avif_decoder_get_icc(d.decoder, unsafe.Pointer(&iccDst[0]), C.size_t(cap(iccDst)))
	return iccDst[:iccLength]
}

func (d *avifDecoder) Description() string {
	return "AVIF"
}

func (d *avifDecoder) BackgroundColor() uint32 {
	return uint32(C.avif_decoder_get_bg_color(d.decoder))
}

func (d *avifDecoder) Duration() time.Duration {
	return time.Duration(C.avif_decoder_get_total_duration(d.decoder)) * time.Millisecond
}

func (d *avifDecoder) LoopCount() int {
	return int(C.avif_decoder_get_loop_count(d.decoder))
}

func (d *avifDecoder) IsAnimated() bool {
	return int(C.avif_decoder_get_num_frames(d.decoder)) > 1
}

func (d *avifDecoder) HasSubtitles() bool {
	return false
}

func (d *avifDecoder) IsStreamable() bool {
	return false
}

func (d *avifDecoder) SkipFrame() error {
	return ErrSkipNotSupported
}

func (d *avifDecoder) Close() {
	C.avif_decoder_release(d.decoder)
	C.opencv_mat_release(d.mat)
	d.buf = nil
}

// Encoder Implementation
// ----------------------------------------

func newAvifEncoder(decodedBy Decoder, dstBuf []byte) (*avifEncoder, error) {
	dstBuf = dstBuf[:1]
	icc := decodedBy.ICC()
	loopCount := decodedBy.LoopCount()
	bgColor := decodedBy.BackgroundColor()

	var enc C.avif_encoder
	if len(icc) > 0 {
		enc = C.avif_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)),
			unsafe.Pointer(&icc[0]), C.size_t(len(icc)), C.int(loopCount))
	} else {
		enc = C.avif_encoder_create(unsafe.Pointer(&dstBuf[0]), C.size_t(cap(dstBuf)),
			nil, 0, C.int(loopCount))
	}
	if enc == nil {
		return nil, ErrBufTooSmall
	}

	return &avifEncoder{
		encoder: enc,
		dstBuf:  dstBuf,
		icc:     icc,
		bgColor: bgColor,
	}, nil
}

func (e *avifEncoder) Encode(f *Framebuffer, opt map[int]int) ([]byte, error) {
	if e.hasFlushed {
		return nil, io.EOF
	}

	if f == nil {
		length := C.avif_encoder_flush(e.encoder)
		if length == 0 {
			return nil, ErrInvalidImage
		}

		e.hasFlushed = true
		return e.dstBuf[:length], nil
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

	frameDelayMs := int(f.duration.Milliseconds())
	length := C.avif_encoder_write(e.encoder, f.mat, firstOpt, C.size_t(len(optList)),
		C.int(frameDelayMs), C.int(f.blend), C.int(f.dispose))
	if length == 0 {
		return nil, ErrInvalidImage
	}

	e.frameIndex++
	return nil, nil
}

func (e *avifEncoder) Close() {
	C.avif_encoder_release(e.encoder)
}
