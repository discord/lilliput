package lilliput

/*
#cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include
#cgo linux CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx -I${SRCDIR}/deps/linux/include
#cgo CXXFLAGS: -std=c++11
#cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include
#cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include
#cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -lavif -lyuv -laom -lavcodec -lavfilter -lavformat -lavutil -lbz2 -lgif -ljpeg -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_photo -lpng -lsharpyuv -lswscale -lwebp -lwebpmux -lz -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework VideoToolbox
#cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/share/OpenCV/3rdparty/lib -lswscale -lavformat -lavcodec -lavfilter -lavutil -lbz2 -lz -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_photo -ljpeg -lpng -lwebp -lz -lgif -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_photo -ljpeg -lpng -lwebp -lsharpyuv -lz -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_photo -ljpeg -lpng -lwebp -lz -lopencv_core -lopencv_imgproc -lopencv_photo -lwebp -lwebpmux -lippicv -lavif -lyuv -laom
void dummy() {}
*/
import "C"

func init() {
	C.dummy()
}
