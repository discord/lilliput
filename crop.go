package lilliput

// #include "opencv.hpp"
import "C"

import (
	"errors"
	"image"
)

// Crop extracts the rectangular sub-region described by rect and writes the
// result into dst, without resizing. rect is clamped to the framebuffer
// bounds. dst must have been created (via NewFramebuffer) large enough to hold
// rect's dimensions; its contents are replaced.
//
// Unlike Fit, which performs a centre crop towards a target aspect ratio, Crop
// extracts an arbitrary, caller-positioned rectangle.
func (f *Framebuffer) Crop(rect image.Rectangle, dst *Framebuffer) error {
	if f == nil || f.mat == nil {
		return ErrFrameBufNoPixels
	}
	if dst == nil {
		return errors.New("lilliput: Crop destination framebuffer is nil")
	}
	rect = rect.Intersect(image.Rect(0, 0, f.width, f.height))
	w, h := rect.Dx(), rect.Dy()
	if w < 1 || h < 1 {
		return errors.New("lilliput: crop rectangle is empty or outside the image")
	}
	cropped := C.opencv_mat_crop(f.mat, C.int(rect.Min.X), C.int(rect.Min.Y), C.int(w), C.int(h))
	if cropped == nil {
		return errors.New("lilliput: opencv_mat_crop returned nil")
	}
	defer C.opencv_mat_release(cropped)
	if err := dst.resizeMat(w, h, f.pixelType); err != nil {
		return err
	}
	C.opencv_mat_resize(cropped, dst.mat, C.int(w), C.int(h), C.CV_INTER_AREA)
	return nil
}
