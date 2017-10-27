package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"github.com/discordapp/lilliput"
)

var EncodeOptions = map[string]map[int]int{
	".jpeg": map[int]int{lilliput.JpegQuality: 85},
	".png":  map[int]int{lilliput.PngCompression: 7},
	".webp": map[int]int{lilliput.WebpQuality: 85},
}

func main() {
	var inputFilename string
	var outputWidth int
	var outputHeight int
	var outputFilename string

	flag.StringVar(&outputFilename, "output", "", "name of output file, also determines output type")
	flag.IntVar(&outputWidth, "width", 0, "width of output file")
	flag.IntVar(&outputHeight, "height", 0, "height of output file")
	flag.Parse()

	if flag.NArg() == 0 {
		fmt.Printf("No input filename provided, quitting.\n")
		os.Exit(1)
	}

	inputFilename = flag.Args()[0]

	inputBuf, err := ioutil.ReadFile(inputFilename)

	// assume image is in inputBuffer of type []byte
	decoder, err := lilliput.NewDecoder(inputBuf)
	// this error reflects very basic checks, mostly
	// just for the magic bytes of the file to match known image formats
	if err != nil {
		fmt.Printf("error decoding image, %s\n", err)
		os.Exit(1)
	}
	defer decoder.Close()

	header, err := decoder.Header()
	// this error is much more comprehensive and reflects
	// format errors
	if err != nil {
		fmt.Printf("error reading image header, %s\n", err)
		os.Exit(1)
	}

	fmt.Printf("image type: %s\n", decoder.Description())
	fmt.Printf("%dpx x %dpx\n", header.Width(), header.Height())

	ops := lilliput.NewImageOps(8192)
	defer ops.Close()

	outputImg := make([]byte, 50*1024*1024)

	outputType := "." + strings.ToLower(decoder.Description())

	if outputFilename != "" {
		outputType = filepath.Ext(outputFilename)
	}

	if outputWidth == 0 {
		outputWidth = header.Width()
	}

	if outputHeight == 0 {
		outputHeight = header.Height()
	}

	opts := &lilliput.ImageOptions{
		FileType:             outputType,
		Width:                outputWidth,
		Height:               outputHeight,
		ResizeMethod:         lilliput.ImageOpsFit,
		NormalizeOrientation: true,
		EncodeOptions:        EncodeOptions[outputType],
	}

	outputImg, err = ops.Transform(decoder, opts, outputImg)
	if err != nil {
		fmt.Printf("error transforming image, %s\n", err)
		os.Exit(1)
	}

	if outputFilename == "" {
		outputFilename = "resized" + filepath.Ext(inputFilename)
	}

	if _, err := os.Stat(outputFilename); !os.IsNotExist(err) {
		fmt.Printf("output filename %s exists, quitting\n", outputFilename)
		os.Exit(1)
	}

	err = ioutil.WriteFile(outputFilename, outputImg, 0400)
	if err != nil {
		fmt.Printf("error writing out resized image, %s\n", err)
		os.Exit(1)
	}

	fmt.Printf("image written to %s\n", outputFilename)
}
