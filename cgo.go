package lilliput

/*
#cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include -I${SRCDIR}/deps/osx/include/opencv4
#cgo linux CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx -I${SRCDIR}/deps/linux/include -I${SRCDIR}/deps/linux/include/opencv4
#cgo CXXFLAGS: -std=c++20
#cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include -I${SRCDIR}/deps/osx/include/opencv4
#cgo linux CXXFLAGS: -I${SRCDIR}/deps/linux/include -I${SRCDIR}/deps/linux/include/opencv4
#cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/lib/opencv4/3rdparty -lavif -lyuv -laom -lavformat -lavcodec -lavutil -lopencv_photo -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lbz2 -lgif -ljpeg -lpng -lswscale -lwebp -lwebpmux -lwebpdemux -lsharpyuv -lz -llibopenjp2 -littnotify -framework Accelerate -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework VideoToolbox
#cgo linux LDFLAGS: -L${SRCDIR}/deps/linux/lib -L${SRCDIR}/deps/linux/lib/opencv4/3rdparty -lavif -lyuv -laom -lavformat -lavcodec -lavutil -lopencv_photo -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lbz2 -lgif -ljpeg -lpng -lswscale -lwebp -lwebpmux -lwebpdemux -lsharpyuv -lz -llibopenjp2 -littnotify -lippiw -lippicv
void dummy() {}
*/
import "C"

func init() {
	C.dummy()
}
