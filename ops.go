package lilliput

import (
	"io"
)

type ImageOpsSizeMethod int

const (
	ImageOpsNoResize ImageOpsSizeMethod = iota
	ImageOpsFit
	// TODO Crop and aspect ratio scaling?
)

type ImageOptions struct {
	FileType             string
	Width                int
	Height               int
	ResizeMethod         ImageOpsSizeMethod
	NormalizeOrientation bool
	EncodeOptions        map[int]int
}

type ImageOps struct {
	frames       []*Framebuffer
	frameIndex   int
	outputBuffer *OutputBuffer
}

func NewImageOps(maxSize int) *ImageOps {
	frames := make([]*Framebuffer, 2)
	frames[0] = NewFramebuffer(maxSize, maxSize)
	frames[1] = NewFramebuffer(maxSize, maxSize)
	return &ImageOps{
		frames:       frames,
		frameIndex:   0,
		outputBuffer: NewOutputBuffer(),
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

func (o *ImageOps) Clear() {
	o.frames[0].Clear()
	o.frames[1].Clear()
	o.outputBuffer.Clear()
}

func (o *ImageOps) Close() {
	o.frames[0].Close()
	o.frames[1].Close()
	o.outputBuffer.Close()
}

func (o *ImageOps) decode(d Decoder) error {
	active := o.active()
	return d.DecodeTo(active)
}

func (o *ImageOps) fit(d Decoder, width, height int) error {
	active := o.active()
	secondary := o.secondary()
	err := active.Fit(width, height, secondary)
	if err != nil {
		return err
	}
	o.swap()
	return nil
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

func (o *ImageOps) Transform(d Decoder, opt *ImageOptions) ([]byte, error) {
	h, err := d.Header()
	if err != nil {
		return nil, err
	}

	enc, err := NewEncoder(opt.FileType, d, o.outputBuffer)
	if err != nil {
		return nil, err
	}
	defer enc.Close()

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

		o.normalizeOrientation(h.Orientation())

		if opt.ResizeMethod == ImageOpsFit {
			o.fit(d, opt.Width, opt.Height)
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

		// content == nil and err == nil -- this is encoder telling us to do another frame

		// for mulitple frames/gifs we need the decoded frame to be active again
		o.swap()
	}
}
