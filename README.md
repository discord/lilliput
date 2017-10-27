# lilliput

[lilliput](https://en.wiktionary.org/wiki/lilliputian#Adjective) resizes images in Go.

Lilliput relies on mature, high-performance C libraries to do most of the work of
decompressing, resizing and compressing images. It aims to do as little memory
allocation as possible and especially not to create garbage in Go. As a result,
it is suitable for very high throughput image resizing services.

Lilliput supports resizing JPEG, PNG, WEBP and GIF. It can also convert formats.
Lilliput also has some support for getting the first frame from MOV and WEBM
videos.

## Example
Lilliput comes with a [fully working example](examples/main.go) that runs on the command line. The
example takes a user supplied filename and prints some basic info about the file.
It then resizes and transcodes the image (if flags are supplied) and saves the
resulting file.

To use the example, `go get github.com/discordapp/lilliput` and then run
`go build` from the examples/ directory.

## Usage

First, `import "github.com/discordapp/lilliput"`.

### Decoder
Lilliput is concerned with in-memory images, so the decoder requires image
data to be in a []byte buffer.

```go
func lilliput.NewDecoder([]byte buf) (lilliput.Decoder, error)
```
Create a new `Decoder` object from the compressed image contained by `buf`.
This will return an error when the magic bytes of the buffer don't match
one of the supported image types.

```go
func (d lilliput.Decoder) Header() (lilliput.ImageHeader, error)
```
Read and return the image's header. The header contains the image's metadata.
Returns error if the image has a malformed header. An image with a malformed
header cannot be decoded.

```go
func (d lilliput.Decoder) Description() string
```
Returns a string describing the image's type, e.g. `"JPEG"` or `"PNG"`.

```go
func (d lilliput.Decoder) DecodeTo(f *lilliput.Framebuffer) error
```
Fully decodes the image and writes its pixel data to `f`. Returns an error
if the decoding process fails.

```go
func (d lilliput.Decoder) Close()
```
Closes the decoder and releases resources. The `Decoder` object must have
`.Close()` called when it is no longer in use.

### ImageHeader
This interface returns basic metadata about an image. It is created by
calling `Decoder.Header()`.

```go
func (h lilliput.ImageHeader) Width() int
```
Returns the image's width in number of pixels.

```go
func (h lilliput.ImageHeader) Height() int
```
Returns the image's height in number of pixels.

```go
func (h lilliput.ImageHeader) PixelType() lilliput.PixelType
```
Returns the basic pixel type for the image's pixels.

```go
func (h lilliput.ImageHeader) Orientation() lilliput.ImageOrientation
```
Returns the metadata-based orientation of the image. This function can
be called on all image types but presently only detects orientation in
JPEG images. An orientation value of 1 indicates default orientation.
All other values indicate some kind of rotation or mirroring.

### PixelType

```go
func (p lilliput.PixelType) Depth() int
```
Returns the number of bits per pixel.

```go
func (p lilliput.PixelType) Channels() int
```
Returns the number of channels per pixel, e.g. 3 for RGB or 4 for RGBA.

### Framebuffer
This type contains a raw array of pixels, decompressed from an image.

`lilliput.Framebuffer` objects support two kinds of resizing.

* `Framebuffer.ResizeTo(width, height int, dst *Framebuffer) error` resizes a
source `Framebuffer` into a destination `Framebuffer`. This method does not preserve
aspect ratio.

* `Framebuffer.Fit(width, height int, dst *Framebuffer) error` resizes a source
`Framebuffer` into a destination `Framebuffer`. This method does preserve aspect
ratio and will do cropping on one dimension if necessary in order to get
the new image to fit within the constraints specified.

The `Framebuffer` object must have `.Close()` called when it is no longer in use.

### Encoding

Once you have a `Framebuffer` containing the pixels as desired, you can
encode them back into one of the supported compressed image formats using
`lilliput.Encoder`.

```go
lilliput.NewEncoder(extension string, decodedBy lilliput.Decoder, dst []byte) (lilliput.Encoder, error)
```
will create a new Encoder object that writes to `dst`. `decodedBy` may be left as `nil`
in most cases but is required when creating a `.gif` encoder. Additionally, `.gif` outputs
require that the input is also `.gif`.

```go
Encoder.Encode(buffer lilliput.Framebuffer, opts map[int]int) error
```
encodes the buffer supplied
into the output `[]byte` given when the `Encoder` was created. `opts` is optional and may be left `nil`.
It is used to control encoder behavior e.g. `map[int]int{lilliput.JpegQuality: 80}` to set JPEG output
quality to `80`.

Valid keys/values for `opts` are `JpegQuality` (1 - 100), `PngCompression` (0 - 9) and `WebpQuality` (0 - 100).

The `Encoder` object must have `.Close()` called when it is no longer in use.

### Ops

The previous process described for transforming images is somewhat burdensome. For that
reason, lilliput also provides the `lilliput.Ops` object that can handle a combined
resize and encode operation on an opened `Decoder`. This object also carefully manages
objects so as to not create much garbage while running.

```go
lilliput.NewImageOps(dimension int)
```
creates an `Ops` object that can operate on images
up to `dimension x dimension` pixels large.

```go
Ops.Transform(decoder lilliput.Decoder, opts *lilliput.ImageOptions, dst []byte) error
```
takes a `Decoder` and decodes the image content within, writing the output to `dst` according to
parameters specified in `opts`.

Fields for `lilliput.ImageOptions` are as follows

* `FileType`: file extension type, e.g. `".jpeg"`

* `Width`: number of pixels of width of output image

* `Height`: number of pixels of height of output image

* `ResizeMethod`: one of `lilliput.ImageOpsNoResize` or `lilliput.ImageOpsFit`. `Fit` behavior
is the same as `Framebuffer.Fit()` described previously.

* `NormalizeOrientation`: If `true`, `Transform()` will inspect the image orientation and
normalize the output so that it is facing in the standard orientation. This will undo
Jpeg EXIF-based orientation.

* `EncodeOptions`: Of type `map[int]int`, same options accepted as `Encoder.Encode`. This
controls output encode quality.

The `Ops` object must have `.Close()` called when it is no longer in use.

## Building Dependencies

Go does not provide any mechanism for arbitrary building of dependencies, e.g. invoking
`make` or `cmake`. In order to make lilliput usable as a standard Go package, prebuilt
static libraries have been provided for all of lilliput's dependencies on Linux and
OSX. In order to automate this process, lilliput ships with build scripts alongside
compressed archives of the sources of its dependencies. These build scripts are provided
for [OSX](deps/build-deps-osx.sh) and [Linux](deps/build-deps-linux.sh).

## License

Lilliput is released under MIT license (see [LICENSE](LICENSE)). Additionally, lilliput ships with other
libraries, each provided under its own license. See [third-party-licenses](third-party-licenses/) for
more info.
