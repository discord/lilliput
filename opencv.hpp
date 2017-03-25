#ifndef C_WRAPPER_H
#define C_WRAPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/types_c.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void *opencv_Mat;
opencv_Mat opencv_createMat(int width, int height, int type);
opencv_Mat opencv_createMatFromData(int width, int height, int type, void *data);
opencv_Mat opencv_imdecode(const opencv_Mat buf, int iscolor, opencv_Mat dst);
bool opencv_imencode(const char *ext, const opencv_Mat image, void *dst, size_t dst_len, const int *params, size_t params_len, int *new_len);
void opencv_resize(const opencv_Mat src, opencv_Mat dst, int width, int height, int interpolation);
opencv_Mat opencv_crop(opencv_Mat src, int x, int y, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
