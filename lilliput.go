package lilliput

import (
	"bytes"
	"errors"
	"strings"
)

var (
	ErrInvalidImage   = errors.New("unrecognized image format")
	ErrDecodingFailed = errors.New("failed to decode image")
	ErrBufTooSmall    = errors.New("buffer too small to hold image")

	gif87Magic = []byte("GIF87a")
	gif89Magic = []byte("GIF89a")
)

type Decoder interface {
	Header() (*ImageHeader, error)
	Close()
	Description() string
	DecodeTo(f *Framebuffer) error
}

type Encoder interface {
	Encode(f *Framebuffer, opt map[int]int) ([]byte, error)
	Close()
}

func isGIF(maybeGIF []byte) bool {
	return bytes.HasPrefix(maybeGIF, gif87Magic) || bytes.HasPrefix(maybeGIF, gif89Magic)
}

func NewDecoder(buf []byte) (Decoder, error) {
	isBufGIF := isGIF(buf)
	if isBufGIF {
		return newGifDecoder(buf)
	}

	maybeDecoder, err := newOpenCVDecoder(buf)
	if err == nil {
		return maybeDecoder, nil
	}

	return newAVCodecDecoder(buf)
}

func NewEncoder(ext string, decodedBy Decoder, dst []byte) (Encoder, error) {
	if strings.ToLower(ext) == ".gif" {
		return newGifEncoder(decodedBy, dst)
	}

	return newOpenCVEncoder(ext, decodedBy, dst)
}
