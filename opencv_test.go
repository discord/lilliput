package lilliput

import (
	"bytes"
	"io/ioutil"
	"testing"
)

func TestAPNG(t *testing.T) {
	pngNoMagic := []byte{
		0, 0, 0, 0, // size
		byte('I'), byte('H'), byte('D'), byte('R'), // type
		0, 0, 0, 0, // crc
	}
	png := append(pngMagic[:], pngNoMagic...)

	if detectAPNG(png) {
		t.Fatalf(`Incorrectly detected APNG in %v`, png)
	}

	apngChunks := [][]byte{
		{0x61, 0x63, 0x54, 0x4c}, // acTL
		{0x66, 0x63, 0x54, 0x4c}, // fcTL
		{0x66, 0x64, 0x41, 0x54}, // fdAT
	}
	for i, chunk := range apngChunks {
		apng := append(png, 0, 0, 0, 0) // size
		apng = append(apng, chunk...)   // type
		apng = append(apng, 0, 0, 0, 0) // crc
		if !detectAPNG(apng) {
			t.Fatalf(`Failed to detect APNG at idx %d in %v`, i, apng)
		}
	}
}

func TestContentLength_PNG_ExtraData(t *testing.T) {
	pngNoMagic := []byte{
		0, 0, 0, 0, // size
		byte('I'), byte('H'), byte('D'), byte('R'), // type
		0, 0, 0, 0, // crc
		0, 0, 0, 4, // size
		1, 2, 3, 4, // type (not real)
		8, 9, 8, 9, // data
		0, 0, 0, 0, // crc
		0, 0, 0, 0, // size
		7, 7, 7, 7, // type
		0, 0, 0, 0, // crc
	}
	png := append(pngMagic[:], pngNoMagic...)

	end := detectContentLength(png)
	if end != len(png) {
		t.Fatalf(`end = "%d", expected "%d"`, end, len(png))
	}

	png = append(png, 56, 56)
	end = detectContentLength(png)
	if end != len(png) {
		t.Fatalf(`end = "%d", expected "%d"`, end, len(png))
	}
}

func TestContentLength_PNG_IEND(t *testing.T) {
	pngNoMagic := []byte{
		0, 0, 0, 0, // size
		byte('I'), byte('H'), byte('D'), byte('R'), // type
		0, 0, 0, 0, // crc
		0, 0, 0, 0, // size
		byte('I'), byte('E'), byte('N'), byte('D'), // type
		0, 0, 0, 0, // crc
	}
	pngExtraData := []byte{
		0, 0, 0, 4, // size
		1, 2, 3, 4, // type
		8, 9, 8, 9, // data
		0, 0, 0, 0, // crc
	}
	png := append(pngMagic[:], pngNoMagic...)
	expectedLength := len(png)
	png = append(png, pngExtraData...)

	end := detectContentLength(png)
	if end != expectedLength {
		t.Fatalf(`end = "%d", expected "%d"`, end, expectedLength)
	}
}

func TestContentLength_JPEG_ExtraData(t *testing.T) {
	jpeg := []byte{
		0xFF, 0xD8, // SOI
		0xFF, 0xE7, 0x00, 0x04, 0xFF, 0xD9, // made up segment
		0xFF, 0xDA, 0x00, 0x04, 0x00, 0x00, // SOS
		0x00, 0x01, 0xD9, 0xFF, 0xD5, 0xD5, // ECS data
		0xFF, 0xD9, // EOI
	}
	result := detectContentLength(jpeg)
	if result != len(jpeg) {
		t.Fatalf(`Expected jpeg content length %d, got %d`, len(jpeg), result)
	}

	extraStuff := []byte{0xFF, 0xC2, 0x00, 0x02}
	jpeg = append(jpeg, extraStuff...)

	result = detectContentLength(jpeg)
	if result != len(jpeg)-len(extraStuff) {
		t.Fatalf(`Expected jpeg content length %d, got %d`, len(jpeg)-len(extraStuff), result)
	}
}

func TestContentLength_JPEG_EntropyCoding(t *testing.T) {
	jpeg := []byte{
		0xFF, 0xD8, // SOI
		0xFF, 0xE7, 0x00, 0x04, 0xFF, 0xD9, // made up data
		0xFF, 0xDA, 0x00, 0x02, // SOS
		0x02, 0x01, 0xFF, 0x00, 0xD9, // ECS data
		0xFF, 0xFF, // padding
		0xFF, 0xD9, // EOI
		0x01, // extra
	}
	result := detectContentLength(jpeg)
	if result != len(jpeg)-1 {
		t.Fatalf(`Expected jpeg content length %d, got %d`, len(jpeg)-1, result)
	}
}

func TestContentLength_Unrecognized(t *testing.T) {
	data := make([]byte, 128)
	result := detectContentLength(data)
	if result != len(data) {
		t.Fatalf(`Expected data content length %d, got %d`, len(data), result)
	}
}

func expectChunks(t *testing.T, png []byte, chunks [][]byte) {
	chunkIter, err := makePngChunkIter(png)
	if err != nil {
		t.Fatalf(`makePngChunkIter failed with error %v`, err)
	}

	chunkIdx := 0
	for chunkIter.next() {
		if chunkIdx >= len(chunks) {
			t.Fatalf(`Found %d chunks, expected only %d`, chunkIdx+1, len(chunks))
		}
		if !bytes.Equal(chunkIter.chunkType(), chunks[chunkIdx]) {
			t.Fatalf(`chunkType = "%v", expected "%v"`, chunkIter.chunkType(), chunks[chunkIdx])
		}
		chunkIdx++
	}
	if chunkIdx < len(chunks) {
		t.Fatalf(`Found %d chunks, expected %d`, chunkIdx, len(chunks))
	}
}

func TestPNGWalk_ExtraData(t *testing.T) {
	pngNoMagic := []byte{
		0, 0, 0, 0, // size
		byte('I'), byte('H'), byte('D'), byte('R'), // type
		0, 0, 0, 0, // crc
		0, 0, 0, 4, // size
		1, 2, 3, 4, // type (not real)
		8, 9, 8, 9, // data
		0, 0, 0, 0, // crc
	}
	png := append(pngMagic[:], pngNoMagic...)

	chunkTypes := [][]byte{
		{byte('I'), byte('H'), byte('D'), byte('R')},
		{1, 2, 3, 4},
	}

	// min chunk size is 12, try extra data up to that amount
	for i := 0; i < 11; i++ {
		png = append(png, 0)
		expectChunks(t, png, chunkTypes)
	}
}

func TestPNGWalk_BadSize(t *testing.T) {
	pngNoMagic := []byte{
		0, 0, 0, 0, // size
		byte('I'), byte('H'), byte('D'), byte('R'), // type
		0, 0, 0, 0, // crc
		0, 128, 0, 4, // size (massive)
		1, 2, 3, 4, // type (not real)
		8, 9, 8, 9, // data
		0, 0, 0, 0, // crc
	}
	png := append(pngMagic[:], pngNoMagic...)

	chunkTypes := [][]byte{
		{byte('I'), byte('H'), byte('D'), byte('R')},
		{1, 2, 3, 4},
	}
	expectChunks(t, png, chunkTypes)
}

func TestPNGWalk_NotPNG(t *testing.T) {
	pngNoMagic := []byte{
		0, 0, 0, 0, // size
		byte('I'), byte('H'), byte('D'), byte('R'), // type
		0, 0, 0, 0, // crc
	}

	_, err := makePngChunkIter(pngNoMagic)
	if err == nil {
		t.Fatalf(`Expected makePngChunkIter to fail, but it did not`)
	}
}

func TestPNGWalk_NoChunks(t *testing.T) {
	pngNoMagic := []byte{}
	png := append(pngMagic[:], pngNoMagic...)

	// min chunk size is 12, try extra data up to that amount
	for i := 0; i < 12; i++ {
		expectChunks(t, png, [][]byte{})
		png = append(png, 0)
	}
}

func TestICC(t *testing.T) {
	tests := []struct {
		name     string
		filePath string
		wantICC  bool
	}{
		{name: "JPEG with ICC Profile", filePath: "testdata/ferry_sunset.jpg", wantICC: true},
		{name: "JPEG without ICC Profile", filePath: "testdata/ferry_sunset_no_icc.jpg", wantICC: false},
		{name: "PNG with ICC Profile", filePath: "testdata/ferry_sunset.png", wantICC: true},
		{name: "PNG without ICC Profile", filePath: "testdata/ferry_sunset_no_icc.png", wantICC: false},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			imgData, err := ioutil.ReadFile(tc.filePath)
			if err != nil {
				t.Fatalf("Failed to read image file: %v", err)
			}
			if len(imgData) == 0 {
				t.Fatalf("Failed to read image file")
			}

			decoder, err := newOpenCVDecoder(imgData)
			if err != nil {
				t.Fatalf("Failed to create decoder: %v", err)
			}

			header, err := decoder.Header()
			if err != nil {
				t.Fatalf("Failed to get the header: %v", err)
			}

			framebuffer := NewFramebuffer(header.width, header.height)
			if err = decoder.DecodeTo(framebuffer); err != nil {
				t.Errorf("DecodeTo failed unexpectedly: %v", err)
			}

			iccData := decoder.ICC()
			if tc.wantICC {
				if len(iccData) == 0 {
					t.Fatalf("Failed to extract ICC profile from the image")
				}
			} else {
				if len(iccData) > 0 {
					t.Fatalf("Extracted ICC profile from the image, but it should not be present")
				}
			}

			// try encoding a WebP image, including ICC profile data when available
			dstBuf := make([]byte, destinationBufferSize)
			encoder, err := newWebpEncoder(decoder, dstBuf)
			if err != nil {
				t.Fatalf("Failed to create a new webp encoder: %v", err)
			}

			options := map[int]int{}
			encodedData, err := encoder.Encode(framebuffer, options)
			if err != nil {
				t.Fatalf("Encode failed unexpectedly: %v", err)
			}
			if encodedData, err = encoder.Encode(nil, options); err != nil {
				t.Fatalf("Encode of empty frame failed unexpectedly: %v", err)
			}
			if len(encodedData) == 0 {
				t.Fatalf("Encoded data is empty, but it should not be")
			}

			decoder.Close()
			encoder.Close()
		})
	}
}

