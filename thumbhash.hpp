#ifndef LILLIPUT_THUMBHASH_HPP
#define LILLIPUT_THUMBHASH_HPP

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct thumbhash_encoder_struct* thumbhash_encoder;

thumbhash_encoder thumbhash_encoder_create(void* buf, size_t buf_len);
int thumbhash_encoder_encode(thumbhash_encoder e, const opencv_mat opqaue_frame);
void thumbhash_encoder_release(thumbhash_encoder e);

#ifdef __cplusplus
}
#endif

#endif
