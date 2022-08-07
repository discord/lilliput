package lilliput

// #cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
// #cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
// #cgo LDFLAGS:  -lwebpmux -lwebpdemux -lwebp
// #cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib
// #cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib
// #include "webpmux.hpp"
import "C"

import (
	"io"
	"time"
	"unsafe"
)

type WebPDecoder struct {
	info        C.WebPAnimInfo
	decoder		*C.WebPAnimDecoder
	lastTime	C.int
}

type WebPEncoder struct {
	encoder		*C.WebPAnimEncoder
	buffer		[]byte
	size        int
	lastTime	C.int
}

func (d *WebPDecoder) Header() (*ImageHeader, error) {
	return &ImageHeader{
		width: 			int(d.info.canvas_width),
		height: 		int(d.info.canvas_height),
		pixelType: 		PixelType(C.CV_8UC4),
		orientation: 	OrientationTopLeft,
		numFrames: 		int(d.info.frame_count),
	}, nil
}

func (d *WebPDecoder) Description() string {
	return "WEBP (Animated)"
}

func (d *WebPDecoder) Duration() time.Duration {
	return 0
}

func (d *WebPDecoder) HasMore() bool {
	return C.WebPAnimDecoderHasMoreFrames(d.decoder) != 0
}

func (d *WebPDecoder) DecodeTo(f *Framebuffer) error {
	if d.decoder == nil {
		return ErrDecodingFailed
	}

	if !d.HasMore() {
		return io.EOF
	}

	err := f.resizeMat(int(d.info.canvas_width), int(d.info.canvas_height), PixelType(C.CV_8UC4))
	if err != nil {
		return err
	}

	res := C.webpmux_decoder_read_data(d.decoder, f.mat)
	if res < 0 {
		return ErrDecodingFailed
	}

	f.duration = time.Duration(res - d.lastTime) * time.Millisecond
	d.lastTime = res

	return nil
}

func (d *WebPDecoder) SkipFrame() error {
	if d.decoder == nil {
		return ErrDecodingFailed
	}

	if !d.HasMore() {
		return io.EOF
	}

	if C.webpmux_decoder_skip_frame(d.decoder) != 0 {
		return ErrDecodingFailed
	}

	return nil
}

func (d *WebPDecoder) Close() {
	if d.decoder != nil {
		C.WebPAnimDecoderDelete(d.decoder)
		d.decoder = nil
	}
}

func (e *WebPEncoder) Encode(frame *Framebuffer, opt map[int]int) ([]byte, error) {
	if e.encoder == nil {
		e.encoder = C.webpmux_create_encoder(C.int(frame.width), C.int(frame.height))
		if e.encoder == nil {
			return nil, ErrBufTooSmall
		}
	}
	
	if frame == nil {
		e.Close()
		return e.buffer[:e.size], nil
	}

	C.webpmux_encoder_add_frame(e.encoder, frame.mat, e.lastTime, C.int(opt[WebpQuality]))
	e.lastTime += C.int(frame.duration / time.Millisecond)

	return nil, nil
}

func (e *WebPEncoder) Close() {
	if e.encoder != nil {
		e.size = int(C.webpmux_encoder_write(e.encoder, unsafe.Pointer(&e.buffer[0]), C.size_t(cap(e.buffer)), e.lastTime))
		e.encoder = nil
	}
}

func newWebPDecoder(buf []byte) (*WebPDecoder, error) {
	retval := &WebPDecoder{
		info: 		C.WebPAnimInfo{},
		decoder: 	nil,
		lastTime: 	C.int(0),
	}
	retval.decoder = C.webpmux_create_decoder(unsafe.Pointer(&buf[0]), C.size_t(len(buf)), unsafe.Pointer(&retval.info))
	if retval.decoder == nil {
		return nil, ErrInvalidImage
	}

	return retval, nil
}

func newWebPEncoder(decodedBy Decoder, dstBuf []byte) (*WebPEncoder, error) {
	return &WebPEncoder{
		encoder:  nil,
		buffer:   dstBuf,
		lastTime: 0,
	}, nil
}
