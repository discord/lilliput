# lilliput

[lilliput](https://en.wiktionary.org/wiki/lilliputian#Adjective) resizes images in Go.

Lilliput relies on mature, high-performance C libraries to do most of the work of
decompressing, resizing and compressing images. It aims to do as little memory
allocation as possible and especially not to create garbage in Go. As a result,
it is suitable for very high throughput image resizing services.

Lilliput supports resizing JPEG, PNG, WEBP and GIF. It can also convert formats.
Lilliput also has some support for getting the first frame from MOV and WEBM
videos.

## Usage

First, `import "github.com/discordapp/lilliput"`.

### Decoding
Lilliput is concerned with in-memory images. So let's first assume we have some
content we think is an image in a `[]byte` (we can get this from a file with
`ioutil.ReadAll`).

```
// assume image is in inputBuffer of type []byte
decoder, err := lilliput.NewDecoder(inputBuf)
// this error reflects very basic checks, mostly
// just for the magic bytes of the file to match known image formats
if err != nil {
    fmt.Printf("error decoding image, %s\n", err)
    return
}
defer decoder.Close()

header, err := decoder.Header()
// this error is much more comprehensive and reflects
// format errors
if err != nil {
    fmt.Printf("error reading image header, %s\n", err)
    return
}

fmt.Printf("image type: %s\n", decoder.Description())
fmt.Printf("%dpx x %dpx\n", header.Width(), header.Height())

// now create a place to store the raw pixels (frame buffer)
frame := lilliput.NewFrameBuffer(4096, 4096)
defer frame.Close()

// finally, decode the image itself
// until we call this, no work will be done in decompressing
// reading the header is cheap and allows us to reject images
// if we don't want to support the type or image size
err = decoder.DecodeTo(frame)
if err != nil {
    fmt.Printf("error decoding image, %s\n", err)
    return
}
```

This example leaves us with a frame buffer containing the content of the
decompressed image (in `frame`).

The `Decoder` object must have `.Close()` called when it is no longer in use.

### Resizing

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

`lilliput.NewEncoder(extension string, decodedBy lilliput.Decoder, dst []byte) (lilliput.Encoder, error)`
will create a new Encoder object that writes to `dst`. `decodedBy` may be left as `nil`
in most cases but is required when creating a `.gif` encoder. Additionally, `.gif` outputs
require that the input is also `.gif`.

`Encoder.Encode(buffer lilliput.Framebuffer, opts map[int]int) error` encodes the buffer supplied
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

`lilliput.NewImageOps(dimension int)` creates an `Ops` object that can operate on images
up to `dimension x dimension` pixels large.

`Ops.Transform(decoder lilliput.Decoder, opts *lilliput.ImageOptions, dst []byte) error` takes
a `Decoder` and decodes the image content within, writing the output to `dst` according to
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
