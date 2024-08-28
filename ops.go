package lilliput

import (
	"io"
	"math"
	"time"
)

type ImageOpsSizeMethod int

const (
	ImageOpsNoResize ImageOpsSizeMethod = iota
	ImageOpsFit
	ImageOpsResize
)

// ImageOptions controls how ImageOps resizes and encodes the
// pixel data decoded from a Decoder
type ImageOptions struct {
	// FileType should be a string starting with '.', e.g.
	// ".jpeg"
	FileType string

	// Width controls the width of the output image
	Width int

	// Height controls the height of the output image
	Height int

	// ResizeMethod controls how the image will be transformed to
	// its output size. Notably, ImageOpsFit will do a cropping
	// resize, while ImageOpsResize will stretch the image.
	ResizeMethod ImageOpsSizeMethod

	// NormalizeOrientation will flip and rotate the image as necessary
	// in order to undo EXIF-based orientation
	NormalizeOrientation bool

	// EncodeOptions controls the encode quality options
	EncodeOptions map[int]int

	// MaxEncodeFrames controls the maximum number of frames that will be resized
	MaxEncodeFrames int

	// MaxEncodeDuration controls the maximum duration of animated image that will be resized
	MaxEncodeDuration time.Duration

	// This is a best effort timeout when encoding multiple frames
	EncodeTimeout time.Duration

	// DisableAnimatedOutput controls the encoder behavior when given a multi-frame input
	DisableAnimatedOutput bool
}

// ImageOps is a reusable object that can resize and encode images.
type ImageOps struct {
	frames        []*Framebuffer
	frameIndex    int
	previousFrame *Framebuffer
}

// NewImageOps creates a new ImageOps object that will operate
// on images up to maxSize on each axis.
func NewImageOps(maxSize int) *ImageOps {
	frames := make([]*Framebuffer, 2)
	frames[0] = NewFramebuffer(maxSize, maxSize)
	frames[1] = NewFramebuffer(maxSize, maxSize)
	return &ImageOps{
		frames:     frames,
		frameIndex: 0,
	}
}

func (o *ImageOps) active() *Framebuffer {
	return o.frames[o.frameIndex]
}

func (o *ImageOps) secondary() *Framebuffer {
	return o.frames[1-o.frameIndex]
}

func (o *ImageOps) swap() {
	o.frameIndex = 1 - o.frameIndex
}

// Clear resets all pixel data in ImageOps. This need not be called
// between calls to Transform. You may choose to call this to remove
// image data from memory.
func (o *ImageOps) Clear() {
	o.frames[0].Clear()
	o.frames[1].Clear()
	if o.previousFrame != nil {
		o.previousFrame.Clear()
	}
}

// Close releases resources associated with ImageOps
func (o *ImageOps) Close() {
	o.frames[0].Close()
	o.frames[1].Close()
	if o.previousFrame != nil {
		o.previousFrame.Close()
		o.previousFrame = nil
	}
}

func (o *ImageOps) decode(d Decoder) error {
	active := o.active()
	return d.DecodeTo(active)
}

func (o *ImageOps) fit(d Decoder, outputCanvasWidth, outputCanvasHeight int) (bool, error) {
	h, err := d.Header()
	if err != nil {
		return false, err
	}
	inputCanvasHeight := h.height
	inputCanvasWidth := h.width

	minHeight := int(math.Min(float64(inputCanvasHeight), float64(outputCanvasHeight)))
	minWidth := int(math.Min(float64(inputCanvasWidth), float64(outputCanvasWidth)))

	active := o.active()
	secondary := o.secondary()

	// Check if scaling is required and if there are non-zero offsets
	if h.IsAnimated() {
		frameXOffset := d.PreviousFrameXOffset()
		frameYOffset := d.PreviousFrameYOffset()

		// Create a 3-channel temporary frame with the same dimensions as the previous frame
		tempFrame := NewFramebuffer(inputCanvasWidth, inputCanvasHeight)
		defer tempFrame.Close() // Ensure that tempFrame is closed in all cases

		// Clear the tempFrame to avoid leftover data
		if err := tempFrame.Create3Channel(inputCanvasWidth, inputCanvasHeight); err != nil {
			return false, err
		}

		// Copy the previous frame to the temporary frame if it exists
		if o.previousFrame != nil {
			if err := tempFrame.CopyToWithOffset(o.previousFrame, inputCanvasWidth, inputCanvasHeight, 0, 0); err != nil {
				return false, err
			}
		} else {
			o.previousFrame = NewFramebuffer(inputCanvasWidth, inputCanvasHeight)
			if err := o.previousFrame.Create3Channel(inputCanvasWidth, inputCanvasHeight); err != nil {
				return false, err
			}
			if err := o.previousFrame.FillWithColor(d.BackgroundColor()); err != nil {
				return false, err
			}
		}

		// Overlay the active frame onto the temporary frame at the original offsets
		if err := tempFrame.CopyToWithOffset(active, inputCanvasWidth, inputCanvasHeight, frameXOffset, frameYOffset); err != nil {
			return false, err
		}

		// store the temporary frame on the ImageOps object
		o.previousFrame.Clear()
		o.previousFrame.CopyToWithOffset(tempFrame, inputCanvasWidth, inputCanvasHeight, 0, 0)

		// Resize the combined frame to the output canvas size
		if err := tempFrame.Fit(outputCanvasWidth, outputCanvasHeight, secondary); err != nil {
			return false, err
		}

		o.swap()
		o.active().duration = d.PreviousFrameDelay()
		o.active().blend = BlendNone
		o.active().dispose = DisposeNone
		o.active().xOffset = 0
		o.active().yOffset = 0

		return true, nil
	}

	if err = active.Fit(minWidth, minHeight, secondary); err != nil {
		return false, err
	}
	o.swap()
	o.active().duration = d.PreviousFrameDelay()
	o.active().blend = d.PreviousFrameBlend()
	o.active().dispose = d.PreviousFrameDispose()
	o.active().xOffset = d.PreviousFrameXOffset()
	o.active().yOffset = d.PreviousFrameYOffset()
	return true, nil
}

func (o *ImageOps) resize(d Decoder, inputCanvasWidth, inputCanvasHeight, outputCanvasWidth, outputCanvasHeight, frameCount int) (bool, error) {
	active := o.active()
	secondary := o.secondary()

	// Calculate scaling factors
	scaleX := float64(outputCanvasWidth) / float64(inputCanvasWidth)
	scaleY := float64(outputCanvasHeight) / float64(inputCanvasHeight)

	// Check if scaling is required and if there are non-zero offsets
	if h, err := d.Header(); err == nil && h.IsAnimated() {
		frameXOffset := d.PreviousFrameXOffset()
		frameYOffset := d.PreviousFrameYOffset()

		// Create a 3-channel temporary frame with the same dimensions as the previous frame
		tempFrame := NewFramebuffer(inputCanvasWidth, inputCanvasHeight)
		defer tempFrame.Close() // Ensure that tempFrame is closed in all cases

		// Clear the tempFrame to avoid leftover data
		if err := tempFrame.Create3Channel(inputCanvasWidth, inputCanvasHeight); err != nil {
			return false, err
		}

		// Copy the previous frame to the temporary frame if it exists
		if o.previousFrame != nil {
			if err := tempFrame.CopyToWithOffset(o.previousFrame, inputCanvasWidth, inputCanvasHeight, 0, 0); err != nil {
				return false, err
			}
		} else {
			o.previousFrame = NewFramebuffer(inputCanvasWidth, inputCanvasHeight)
			if err := o.previousFrame.Create3Channel(inputCanvasWidth, inputCanvasHeight); err != nil {
				return false, err
			}
			if err := o.previousFrame.FillWithColor(d.BackgroundColor()); err != nil {
				return false, err
			}
		}

		// Overlay the active frame onto the temporary frame at the original offsets
		if err := tempFrame.CopyToWithOffset(active, inputCanvasWidth, inputCanvasHeight, frameXOffset, frameYOffset); err != nil {
			return false, err
		}

		// store the temporary frame on the ImageOps object
		o.previousFrame.Clear()
		o.previousFrame.CopyToWithOffset(tempFrame, inputCanvasWidth, inputCanvasHeight, 0, 0)

		// Resize the combined frame to the output canvas size
		if err := tempFrame.ResizeTo(outputCanvasWidth, outputCanvasHeight, secondary); err != nil {
			return false, err
		}

		o.swap()
		o.active().duration = d.PreviousFrameDelay()
		o.active().blend = BlendNone
		o.active().dispose = DisposeNone
		o.active().xOffset = 0
		o.active().yOffset = 0

		return true, nil
	}

	// Resize normally if no scaling is required or there are no offsets
	newFrameWidth := int(float64(active.Width()) * scaleX)
	newFrameHeight := int(float64(active.Height()) * scaleY)

	err := active.ResizeTo(newFrameWidth, newFrameHeight, secondary)
	if err != nil {
		return false, err
	}
	o.swap()
	o.active().duration = d.PreviousFrameDelay()
	o.active().blend = d.PreviousFrameBlend()
	o.active().dispose = d.PreviousFrameDispose()
	o.active().xOffset = d.PreviousFrameXOffset()
	o.active().yOffset = d.PreviousFrameYOffset()

	return true, nil
}

func (o *ImageOps) normalizeOrientation(orientation ImageOrientation) {
	active := o.active()
	active.OrientationTransform(orientation)
}

func (o *ImageOps) encode(e Encoder, opt map[int]int) ([]byte, error) {
	active := o.active()
	return e.Encode(active, opt)
}

func (o *ImageOps) encodeEmpty(e Encoder, opt map[int]int) ([]byte, error) {
	return e.Encode(nil, opt)
}

func (o *ImageOps) skipToEnd(d Decoder) error {
	var err error
	for {
		err = d.SkipFrame()
		if err != nil {
			return err
		}
	}
}

// Transform performs the requested transform operations on the Decoder specified by d.
// The result is written into the output buffer dst. A new slice pointing to dst is returned
// with its length set to the length of the resulting image. Errors may occur if the decoded
// image is too large for ImageOps or if Encoding fails.
//
// It is important that .Decode() not have been called already on d.
func (o *ImageOps) Transform(d Decoder, opt *ImageOptions, dst []byte) ([]byte, error) {
	defer func() {
		// ensure that the previous frame is closed in all cases and memory is released
		if o.previousFrame != nil {
			o.previousFrame.Close()
			o.previousFrame = nil
		}
	}()

	h, err := d.Header()
	if err != nil {
		return nil, err
	}

	enc, err := NewEncoder(opt.FileType, d, dst)
	if err != nil {
		return nil, err
	}
	defer enc.Close()

	frameCount := 0
	duration := time.Duration(0)
	encodeTimeoutTime := time.Now().Add(opt.EncodeTimeout)

	for {
		// break out if we're creating a single frame and we've already done one
		if opt.DisableAnimatedOutput && frameCount > 0 {
			return o.encodeEmpty(enc, opt.EncodeOptions)
		}
		err = o.decode(d)
		emptyFrame := false
		if err != nil {
			if err != io.EOF {
				return nil, err
			}
			// io.EOF means we are out of frames, so we should signal to encoder to wrap up
			emptyFrame = true
		}

		duration += o.active().Duration()

		if opt.MaxEncodeDuration != 0 && duration > opt.MaxEncodeDuration {
			err = o.skipToEnd(d)
			if err != io.EOF {
				return nil, err
			}
			return o.encodeEmpty(enc, opt.EncodeOptions)
		}

		o.normalizeOrientation(h.Orientation())

		var swapped bool
		if !emptyFrame {
			if opt.ResizeMethod == ImageOpsFit {
				swapped, err = o.fit(d, opt.Width, opt.Height)
			} else if opt.ResizeMethod == ImageOpsResize {
				swapped, err = o.resize(d, h.Width(), h.Height(), opt.Width, opt.Height, frameCount)
			} else {
				if h.IsAnimated() {
					swapped, err = o.fit(d, h.Width(), h.Height())
				} else {
					swapped, err = false, nil
				}
			}

			if err != nil {
				return nil, err
			}
		}

		var content []byte
		if emptyFrame {
			content, err = o.encodeEmpty(enc, opt.EncodeOptions)
		} else {
			content, err = o.encode(enc, opt.EncodeOptions)
		}

		if err != nil {
			return nil, err
		}

		if content != nil {
			return content, nil
		}

		frameCount++

		if opt.MaxEncodeFrames != 0 && frameCount == opt.MaxEncodeFrames {
			err = o.skipToEnd(d)
			if err != io.EOF {
				return nil, err
			}
			return o.encodeEmpty(enc, opt.EncodeOptions)
		}

		if time.Now().After(encodeTimeoutTime) {
			return nil, ErrEncodeTimeout
		}

		// content == nil and err == nil -- this is encoder telling us to do another frame

		// for mulitple frames/gifs we need the decoded frame to be active again
		if swapped {
			o.swap()
		}
	}
}
