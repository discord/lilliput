package lilliput

import "sync"

const MAX_ICC_PROFILE_SIZE = 8192

// ICCBuffer is a custom type that wraps a byte slice and tracks the used length
type ICCBuffer struct {
	buf        []byte
	usedLength int
}

// NewICCBuffer creates a new ICCBuffer with a predefined size
func NewICCBuffer() *ICCBuffer {
	return &ICCBuffer{
		buf: make([]byte, MAX_ICC_PROFILE_SIZE),
	}
}

// Reset zeros out the used portion of the buffer
func (b *ICCBuffer) Reset() {
	if b.usedLength != 0 {
		for i := 0; i < b.usedLength; i++ {
			b.buf[i] = 0
		}
		b.usedLength = 0
	}
}

// Get returns the underlying byte slice resized to the used length
func (b *ICCBuffer) Get() []byte {
	return b.buf[:b.usedLength]
}

// ICCBuffer pool
var iccBufferPool *sync.Pool

func init() {
	iccBufferPool = &sync.Pool{
		New: func() interface{} {
			return NewICCBuffer()
		},
	}
}

// acquireICCBuffer gets a buffer from the pool
func acquireICCBuffer() *ICCBuffer {
	return iccBufferPool.Get().(*ICCBuffer)
}

// releaseICCBuffer resets the buffer and puts it back into the pool
func releaseICCBuffer(buf *ICCBuffer) {
	buf.Reset()
	iccBufferPool.Put(buf)
}
