package lilliput

import (
	"fmt"
	"image"
	"io"
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
	frames                  []*Framebuffer
	frameIndex              int
	animatedCompositeBuffer *Framebuffer
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
	if o.animatedCompositeBuffer != nil {
		o.animatedCompositeBuffer.Clear()
	}
}

// Close releases resources associated with ImageOps
func (o *ImageOps) Close() {
	o.frames[0].Close()
	o.frames[1].Close()
	if o.animatedCompositeBuffer != nil {
		o.animatedCompositeBuffer.Close()
		o.animatedCompositeBuffer = nil
	}
}

// setupAnimatedFrameBuffers sets up the animated frame buffer.
// It returns an error if the frame could not be created.
func (o *ImageOps) setupAnimatedFrameBuffers(d Decoder, inputCanvasWidth, inputCanvasHeight int, hasAlpha bool) error {
	// Create a buffer to hold the composite of the current frame and the previous frame
	if o.animatedCompositeBuffer == nil {
		o.animatedCompositeBuffer = NewFramebuffer(inputCanvasWidth, inputCanvasHeight)
		if !hasAlpha {
			if err := o.animatedCompositeBuffer.Create3Channel(inputCanvasWidth, inputCanvasHeight); err != nil {
				return err
			}
		} else {
			if err := o.animatedCompositeBuffer.Create4Channel(inputCanvasWidth, inputCanvasHeight); err != nil {
				return err
			}
		}
		rect := image.Rect(0, 0, inputCanvasWidth, inputCanvasHeight)
		return o.animatedCompositeBuffer.FillWithColor(d.BackgroundColor(), rect)
	}

	return nil
}

// decode decodes the active frame from the decoder specified by d.
func (o *ImageOps) decode(d Decoder) error {
	active := o.active()
	return d.DecodeTo(active)
}

// fit fits the active frame to the specified output canvas size.
// It returns true if the frame was resized and false if it was not.
// It returns an error if the frame could not be resized.
func (o *ImageOps) fit(d Decoder, inputCanvasWidth, inputCanvasHeight, outputCanvasWidth, outputCanvasHeight int, isAnimated, hasAlpha bool) (bool, error) {
	newWidth, newHeight := calculateExpectedSize(inputCanvasWidth, inputCanvasHeight, outputCanvasWidth, outputCanvasHeight)

	if isAnimated {
		if err := o.setupAnimatedFrameBuffers(d, inputCanvasWidth, inputCanvasHeight, hasAlpha); err != nil {
			return false, err
		}

		if err := o.applyDisposeMethod(d); err != nil {
			return false, err
		}

		if err := o.applyBlendMethod(d); err != nil {
			return false, err
		}

		if err := o.animatedCompositeBuffer.Fit(newWidth, newHeight, o.secondary()); err != nil {
			return false, err
		}

		o.copyFramePropertiesAndSwap()
		return true, nil
	}

	// If the image is not animated, we can fit it directly.
	if err := o.active().Fit(newWidth, newHeight, o.secondary()); err != nil {
		return false, err
	}
	o.copyFramePropertiesAndSwap()
	return true, nil
}

// resize resizes the active frame to the specified output canvas size.
func (o *ImageOps) resize(d Decoder, inputCanvasWidth, inputCanvasHeight, outputCanvasWidth, outputCanvasHeight, frameCount int, isAnimated, hasAlpha bool) (bool, error) {
	// If the image is animated, we need to resize the frame to the input canvas size
	// and then copy the previous frame's data to the working buffer.
	if isAnimated {
		if err := o.setupAnimatedFrameBuffers(d, inputCanvasWidth, inputCanvasHeight, hasAlpha); err != nil {
			return false, err
		}

		if err := o.applyDisposeMethod(d); err != nil {
			return false, err
		}

		if err := o.applyBlendMethod(d); err != nil {
			return false, err
		}

		if err := o.animatedCompositeBuffer.ResizeTo(outputCanvasWidth, outputCanvasHeight, o.secondary()); err != nil {
			return false, err
		}

		o.copyFramePropertiesAndSwap()
		return true, nil
	}

	if err := o.active().ResizeTo(outputCanvasWidth, outputCanvasHeight, o.secondary()); err != nil {
		return false, err
	}
	o.copyFramePropertiesAndSwap()

	return true, nil
}

func calculateExpectedSize(origWidth, origHeight, reqWidth, reqHeight int) (int, int) {
	if reqWidth == reqHeight && reqWidth > min(origWidth, origHeight) {
		// Square resize request larger than smaller original dimension
		minDim := min(origWidth, origHeight)
		return minDim, minDim
	} else if reqWidth > origWidth && reqHeight > origHeight && reqWidth != reqHeight {
		// Both dimensions larger than original and not square
		return origWidth, origHeight
	} else {
		// All other cases
		return reqWidth, reqHeight
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// normalizeOrientation flips and rotates the active frame to undo EXIF orientation.
func (o *ImageOps) normalizeOrientation(orientation ImageOrientation) {
	active := o.active()
	active.OrientationTransform(orientation)
}

// encode encodes the active frame using the encoder specified by e.
func (o *ImageOps) encode(e Encoder, opt map[int]int) ([]byte, error) {
	active := o.active()
	return e.Encode(active, opt)
}

// encodeEmpty encodes an empty frame using the encoder specified by e.
func (o *ImageOps) encodeEmpty(e Encoder, opt map[int]int) ([]byte, error) {
	return e.Encode(nil, opt)
}

// skipToEnd skips to the end of the animation specified by d.
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
		if o.animatedCompositeBuffer != nil {
			o.animatedCompositeBuffer.Close()
			o.animatedCompositeBuffer = nil
		}
	}()

	inputHeader, enc, err := o.initializeTransform(d, opt, dst)
	if err != nil {
		return nil, err
	}
	defer enc.Close()

	frameCount := 0
	duration := time.Duration(0)
	encodeTimeoutTime := time.Now().Add(opt.EncodeTimeout)

	// transform the frames and encode them until we run out of frames or the timeout is reached
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

		o.normalizeOrientation(inputHeader.Orientation())

		// transform the frame, resizing if necessary
		var swapped bool
		if !emptyFrame {
			swapped, err = o.transformCurrentFrame(d, opt, inputHeader, frameCount)
			if err != nil {
				return nil, err
			}
		}

		// encode the frame to the output buffer
		var content []byte
		if emptyFrame {
			content, err = o.encodeEmpty(enc, opt.EncodeOptions)
		} else {
			content, err = o.encode(enc, opt.EncodeOptions)
		}

		// content == nil and err == nil -- this is encoder telling us to do another frame
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

		// for mulitple frames/gifs we need the decoded frame to be active again
		if swapped {
			o.swap()
		}
	}
}

// transformCurrentFrame transforms the current frame using the decoder specified by d.
// It returns true if the frame was resized and false if it was not.
// It returns an error if the frame could not be resized.
func (o *ImageOps) transformCurrentFrame(d Decoder, opt *ImageOptions, inputHeader *ImageHeader, frameCount int) (bool, error) {
	if opt.ResizeMethod == ImageOpsNoResize && !inputHeader.IsAnimated() {
		return false, nil
	}

	outputWidth, outputHeight := opt.Width, opt.Height
	if opt.ResizeMethod == ImageOpsNoResize {
		outputWidth, outputHeight = inputHeader.Width(), inputHeader.Height()
	}

	switch opt.ResizeMethod {
	case ImageOpsFit, ImageOpsNoResize:
		return o.fit(d, inputHeader.Width(), inputHeader.Height(), outputWidth, outputHeight, inputHeader.IsAnimated(), inputHeader.HasAlpha())
	case ImageOpsResize:
		return o.resize(d, inputHeader.Width(), inputHeader.Height(), outputWidth, outputHeight, frameCount, inputHeader.IsAnimated(), inputHeader.HasAlpha())
	default:
		return false, fmt.Errorf("unknown resize method: %v", opt.ResizeMethod)
	}
}

// initializeTransform initializes the transform process.
// It returns the image header, encoder, and error.
func (o *ImageOps) initializeTransform(d Decoder, opt *ImageOptions, dst []byte) (*ImageHeader, Encoder, error) {
	inputHeader, err := d.Header()
	if err != nil {
		return nil, nil, err
	}

	enc, err := NewEncoder(opt.FileType, d, dst)
	if err != nil {
		return nil, nil, err
	}

	return inputHeader, enc, nil
}

func (o *ImageOps) applyDisposeMethod(d Decoder) error {
	active := o.active()
	switch active.dispose {
	case DisposeToBackgroundColor:
		rect := image.Rect(active.xOffset, active.yOffset, active.xOffset+active.Width(), active.yOffset+active.Height())
		return o.animatedCompositeBuffer.FillWithColor(d.BackgroundColor(), rect)
	case NoDispose:
		// Do nothing
	}
	return nil
}

func (o *ImageOps) applyBlendMethod(d Decoder) error {
	active := o.active()
	rect := image.Rect(
		active.xOffset,
		active.yOffset,
		active.xOffset+active.Width(),
		active.yOffset+active.Height(),
	)

	switch active.blend {
	case UseAlphaBlending:
		return o.animatedCompositeBuffer.CopyToOffsetWithAlphaBlending(active, rect)
	case NoBlend:
		return o.animatedCompositeBuffer.CopyToOffsetNoBlend(active, rect)
	}
	return nil
}

// copyFrameProperties copies the properties from the active frame to the secondary frame
// and then swaps the frames.
func (o *ImageOps) copyFramePropertiesAndSwap() {
	o.secondary().duration = o.active().duration
	o.secondary().dispose = o.active().dispose
	o.secondary().blend = o.active().blend
	o.swap()
}
