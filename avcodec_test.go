package lilliput

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func createAtom(atomType string, size uint32) []byte {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.BigEndian, size)
	buf.WriteString(atomType)
	buf.Write(make([]byte, size-8)) // Fill the rest of the atom with zeros
	return buf.Bytes()
}

func TestIsStreamable(t *testing.T) {
	// Create a mock MP4 file with 'moov' before 'mdat'
	streamableData := append(createAtom("moov", 12), createAtom("mdat", 12)...)

	// Create a mock MP4 file with 'mdat' before 'moov'
	nonStreamableData := append(createAtom("mdat", 12), createAtom("moov", 12)...)

	// Create a mock MP4 file with atom incomplete (spans beyond buffer)
	incompleteAtomData := createAtom("ftyp", 5000)[:20]

	// Create a mock MP4 file with atom larger than probe limit
	largeAtomData := append(createAtom("ftyp", 12), createAtom("ftyp", probeBytesLimit)...)

	// Create a mock MP4 file with some other atom before 'moov'
	streamableDataWithFtypAtom := append(createAtom("ftyp", 12), streamableData...)

	// Test cases
	tests := []struct {
		name string
		data []byte
		want bool
	}{
		{"Streamable", streamableData, true},
		{"NonStreamable", nonStreamableData, false},
		{"IncompleteAtom", incompleteAtomData, false},
		{"LargeAtom", largeAtomData, false},
		{"StreamableWithFtypAtom", streamableDataWithFtypAtom, true},
	}

	// Run tests
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			d := avCodecDecoder{buf: tt.data}
			if got := d.IsStreamable(); got != tt.want {
				t.Errorf("IsStreamable() = %v, want %v", got, tt.want)
			}
		})
	}
}
