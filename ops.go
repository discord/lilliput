package opencv

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
	frames     []*Framebuffer
	frameIndex int
}

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

func (o *ImageOps) Clear() {
	o.frames[0].Clear()
	o.frames[1].Clear()
}

func (o *ImageOps) Close() {
	o.frames[0].Close()
	o.frames[1].Close()
}

func (o *ImageOps) decode(d Decoder) error {
	active := o.active()
	err := d.DecodeTo(active)
	if err != nil {
		return err
	}

	return nil
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
	content, err := e.Encode(active, opt)
	return content, err
}

func (o *ImageOps) Transform(d Decoder, opt *ImageOptions) ([]byte, error) {
	h, err := d.Header()
	if err != nil {
		return nil, err
	}

	err = o.decode(d)
	if err != nil {
		return nil, err
	}

	o.normalizeOrientation(h.Orientation())

	if opt.ResizeMethod == ImageOpsFit {
		o.fit(d, opt.Width, opt.Height)
	}

	enc, err := NewEncoder(opt.FileType)
	if err != nil {
		return nil, err
	}
	defer enc.Close()

	return o.encode(enc, opt.EncodeOptions)
}
