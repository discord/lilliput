package opencv

// #include "opencv.hpp"
// #cgo linux pkg-config: opencv
// #cgo darwin pkg-config: opencv
import "C"

import (
	"unsafe"
)

type Framebuffer struct {
	mat C.opencv_Mat
}

// Allocates a new opencv framebuffer of dimensions width and height
// Assumes 8-bit depth, 3 color channels
func NewFramebuffer(width, height int) *Framebuffer {
	return &Framebuffer{
		mat: C.opencv_createMat(C.int(width), C.int(height), C.CV_8U),
	}
}

func (f *Framebuffer) Decode(encoded []byte) error {
	encodedMat := C.opencv_createMatFromData(C.int(len(encoded)), 1, C.CV_8U, unsafe.Pointer(&encoded[0]))
	f.mat = C.opencv_imdecode(encodedMat, 1, f.mat)
	return nil
}

func (f *Framebuffer) ResizeTo(dst *Framebuffer, width, height int) {
	C.opencv_resize(f.mat, dst.mat, C.int(width), C.int(height), C.CV_INTER_LANCZOS4)
}

func (f *Framebuffer) Encode(Extension string, dst []byte) []byte {
	var newLen int
	C.opencv_imencode(C.CString(Extension), f.mat, unsafe.Pointer(&dst[0]), C.size_t(cap(dst)), nil, 0, (*C.int)(unsafe.Pointer(&newLen)))
	return dst[:newLen]
}
