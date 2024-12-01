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
// on images up to maxSize on each axis. It initializes two framebuffers
// for double-buffering operations.
func NewImageOps(maxSize int) *ImageOps {
	frames := make([]*Framebuffer, 2)
	frames[0] = NewFramebuffer(maxSize, maxSize)
	frames[1] = NewFramebuffer(maxSize, maxSize)
	return &ImageOps{
		frames:     frames,
		frameIndex: 0,
	}
}

// active returns the currently active framebuffer used for operations
func (o *ImageOps) active() *Framebuffer {
	return o.frames[o.frameIndex]
}

// secondary returns the secondary framebuffer used for double-buffering operations
func (o *ImageOps) secondary() *Framebuffer {
	return o.frames[1-o.frameIndex]
}

// swap toggles between the active and secondary framebuffers
func (o *ImageOps) swap() {
	o.frameIndex = 1 - o.frameIndex
}

// Clear frees the pixel data held in all framebuffers. While not required between
// Transform operations, you can call this to reduce memory usage when the ImageOps
// object will be idle for a while.
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

// setupAnimatedFrameBuffers initializes the composite buffer needed for animated image processing.
// It creates a buffer with the appropriate number of channels based on whether the image has alpha.
// Returns an error if buffer creation fails.
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
		return o.animatedCompositeBuffer.ClearToTransparent(rect)
	}

	return nil
}

// decode reads the current frame from the decoder into the active framebuffer.
// Returns an error if decoding fails.
func (o *ImageOps) decode(d Decoder) error {
	active := o.active()
	return d.DecodeTo(active)
}

// fit resizes the active frame to fit within the specified dimensions while maintaining aspect ratio.
// For animated images, it handles frame compositing and disposal.
// Returns (true, nil) if resizing was performed successfully, (false, error) if an error occurred.
func (o *ImageOps) fit(d Decoder, inputCanvasWidth, inputCanvasHeight, outputCanvasWidth, outputCanvasHeight int, isAnimated, hasAlpha bool) (bool, error) {
	newWidth, newHeight := calculateExpectedSize(inputCanvasWidth, inputCanvasHeight, outputCanvasWidth, outputCanvasHeight)

	if isAnimated {
		if err := o.setupAnimatedFrameBuffers(d, inputCanvasWidth, inputCanvasHeight, hasAlpha); err != nil {
			return false, err
		}

		// blend transparent pixels of the active frame with corresponding pixels of the previous canvas, creating a composite
		if err := o.applyBlendMethod(d); err != nil {
			return false, err
		}

		// resize the composite to the output canvas size
		if err := o.animatedCompositeBuffer.Fit(newWidth, newHeight, o.secondary()); err != nil {
			return false, err
		}

		// apply dispose method of the active frame to the composite
		if err := o.applyDisposeMethod(d); err != nil {
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

// resize scales the active frame to exactly match the specified dimensions.
// For animated images, it handles frame compositing and disposal.
// Returns (true, nil) if resizing was performed successfully, (false, error) if an error occurred.
func (o *ImageOps) resize(d Decoder, inputCanvasWidth, inputCanvasHeight, outputCanvasWidth, outputCanvasHeight, frameCount int, isAnimated, hasAlpha bool) (bool, error) {
	// If the image is animated, we need to resize the frame to the input canvas size
	// and then copy the previous frame's data to the working buffer.
	if isAnimated {
		if err := o.setupAnimatedFrameBuffers(d, inputCanvasWidth, inputCanvasHeight, hasAlpha); err != nil {
			return false, err
		}

		if err := o.applyBlendMethod(d); err != nil {
			return false, err
		}

		if err := o.animatedCompositeBuffer.ResizeTo(outputCanvasWidth, outputCanvasHeight, o.secondary()); err != nil {
			return false, err
		}

		if err := o.applyDisposeMethod(d); err != nil {
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

// calculateExpectedSize determines the final dimensions for an image based on
// original and requested sizes, handling special cases for square resizing
// and oversized requests.
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

// min returns the smaller of two integers
func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// normalizeOrientation applies EXIF orientation corrections to the active frame
// by performing the necessary flips and rotations.
func (o *ImageOps) normalizeOrientation(orientation ImageOrientation) {
	active := o.active()
	active.OrientationTransform(orientation)
}

// encode writes the active frame to an encoded format using the provided encoder
// and encoding options. Returns the encoded bytes or an error.
func (o *ImageOps) encode(e Encoder, opt map[int]int) ([]byte, error) {
	active := o.active()
	return e.Encode(active, opt)
}

// encodeEmpty signals the encoder to finalize the encoding process without
// additional frame data. Used for handling animation termination.
func (o *ImageOps) encodeEmpty(e Encoder, opt map[int]int) ([]byte, error) {
	return e.Encode(nil, opt)
}

// skipToEnd advances the decoder to the final frame of an animation.
// Returns io.EOF when the end is reached or an error if seeking fails.
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

		// break out if we're creating a single frame and we've already done one
		if opt.DisableAnimatedOutput {
			return o.encodeEmpty(enc, opt.EncodeOptions)
		}

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

// transformCurrentFrame applies the requested resize operation to the current frame.
// Handles both static and animated images, managing frame compositing when needed.
// Returns (true, nil) if transformation was performed, (false, error) if an error occurred.
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

// initializeTransform prepares for image transformation by reading the input header
// and creating an appropriate encoder. Returns the header, encoder, and any error.
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

// applyDisposeMethod handles frame disposal according to the active frame's
// dispose method in animated images. For frames marked with DisposeToBackgroundColor,
// it clears the affected region to transparent. For NoDispose, the previous frame's
// content is preserved.
func (o *ImageOps) applyDisposeMethod(d Decoder) error {
	active := o.active()
	switch active.dispose {
	case DisposeToBackgroundColor:
		rect := image.Rect(active.xOffset, active.yOffset, active.xOffset+active.Width(), active.yOffset+active.Height())
		return o.animatedCompositeBuffer.ClearToTransparent(rect)
	case NoDispose:
		// Do nothing
	}
	return nil
}

// applyBlendMethod composites the active frame onto the animation buffer using
// the specified blending mode (alpha blending or direct copy).
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

// copyFramePropertiesAndSwap transfers animation metadata (duration, disposal method,
// and blend mode) from the active frame to the secondary frame, then swaps buffers.
func (o *ImageOps) copyFramePropertiesAndSwap() {
	o.secondary().duration = o.active().duration
	o.secondary().dispose = o.active().dispose
	o.secondary().blend = o.active().blend
	o.swap()
}
