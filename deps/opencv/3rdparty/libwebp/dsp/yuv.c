// Copyright 2010 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// YUV->RGB conversion function
//
// Author: Skal (pascal.massimino@gmail.com)

#include "./yuv.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#ifdef WEBP_YUV_USE_TABLE

int16_t VP8kVToR[256], VP8kUToB[256];
int32_t VP8kVToG[256], VP8kUToG[256];
uint8_t VP8kClip[YUV_RANGE_MAX - YUV_RANGE_MIN];
uint8_t VP8kClip4Bits[YUV_RANGE_MAX - YUV_RANGE_MIN];

static int done = 0;

static WEBP_INLINE uint8_t clip(int v, int max_value) {
  return v < 0 ? 0 : v > max_value ? max_value : v;
}

void VP8YUVInit(void) {
  int i;
  if (done) {
    return;
  }
#ifndef USE_YUVj
  for (i = 0; i < 256; ++i) {
    VP8kVToR[i] = (89858 * (i - 128) + YUV_HALF) >> YUV_FIX;
    VP8kUToG[i] = -22014 * (i - 128) + YUV_HALF;
    VP8kVToG[i] = -45773 * (i - 128);
    VP8kUToB[i] = (113618 * (i - 128) + YUV_HALF) >> YUV_FIX;
  }
  for (i = YUV_RANGE_MIN; i < YUV_RANGE_MAX; ++i) {
    const int k = ((i - 16) * 76283 + YUV_HALF) >> YUV_FIX;
    VP8kClip[i - YUV_RANGE_MIN] = clip(k, 255);
    VP8kClip4Bits[i - YUV_RANGE_MIN] = clip((k + 8) >> 4, 15);
  }
#else
  for (i = 0; i < 256; ++i) {
    VP8kVToR[i] = (91881 * (i - 128) + YUV_HALF) >> YUV_FIX;
    VP8kUToG[i] = -22554 * (i - 128) + YUV_HALF;
    VP8kVToG[i] = -46802 * (i - 128);
    VP8kUToB[i] = (116130 * (i - 128) + YUV_HALF) >> YUV_FIX;
  }
  for (i = YUV_RANGE_MIN; i < YUV_RANGE_MAX; ++i) {
    const int k = i;
    VP8kClip[i - YUV_RANGE_MIN] = clip(k, 255);
    VP8kClip4Bits[i - YUV_RANGE_MIN] = clip((k + 8) >> 4, 15);
  }
#endif

  done = 1;
}

#else

void VP8YUVInit(void) {}

#endif  // WEBP_YUV_USE_TABLE

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
