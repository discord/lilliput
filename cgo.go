package lilliput

/*
#cgo darwin CFLAGS: -I${SRCDIR}/deps/osx/include -I${SRCDIR}/deps/osx/include/opencv4
#cgo linux,amd64 CFLAGS: -msse -msse2 -msse3 -msse4.1 -msse4.2 -mavx -I${SRCDIR}/deps/linux/amd64/include -I${SRCDIR}/deps/linux/amd64/include/opencv4
#cgo linux,arm64 CFLAGS: -march=armv8-a+simd+crypto -I${SRCDIR}/deps/linux/aarch64/include -I${SRCDIR}/deps/linux/aarch64/include/opencv4
#cgo CXXFLAGS: -std=c++20
#cgo darwin CXXFLAGS: -I${SRCDIR}/deps/osx/include -I${SRCDIR}/deps/osx/include/opencv4
#cgo linux,amd64 CXXFLAGS: -I${SRCDIR}/deps/linux/amd64/include -I${SRCDIR}/deps/linux/amd64/include/opencv4
#cgo linux,arm64 CXXFLAGS: -I${SRCDIR}/deps/linux/aarch64/include -I${SRCDIR}/deps/linux/aarch64/include/opencv4
#cgo darwin LDFLAGS: -L${SRCDIR}/deps/osx/lib -L${SRCDIR}/deps/osx/lib/opencv4/3rdparty -lavif -lyuv -laom -ldav1d -lavformat -lavcodec -lavutil -lopencv_photo -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lbz2 -lgif -ljpeg -lpng -lswscale -lwebp -lwebpmux -lwebpdemux -lsharpyuv -lz -llibopenjp2 -littnotify -framework Accelerate -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework VideoToolbox -llcms2
#cgo linux,amd64 LDFLAGS: -L${SRCDIR}/deps/linux/amd64/lib -L${SRCDIR}/deps/linux/amd64/lib/opencv4/3rdparty -lavif -lyuv -laom -ldav1d -lavformat -lavcodec -lavutil -lopencv_photo -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lbz2 -lgif -ljpeg -lpng16 -lswscale -lwebp -lwebpmux -lwebpdemux -lsharpyuv -lz -llibopenjp2 -littnotify -lippiw -lippicv -llcms2
#cgo linux,arm64 LDFLAGS: -L${SRCDIR}/deps/linux/aarch64/lib -L${SRCDIR}/deps/linux/aarch64/lib/opencv4/3rdparty -lavif -lyuv -laom -ldav1d -lavformat -lavcodec -lavutil -lopencv_photo -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lbz2 -lgif -ljpeg -lpng16 -lswscale -lwebp -lwebpmux -lwebpdemux -lsharpyuv -lz -llibopenjp2 -littnotify -llcms2
void dummy() {}
*/
import "C"

func init() {
	C.dummy()
}
