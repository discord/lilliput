#include "thumbhash.hpp"
#include <stdbool.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <tuple>

typedef std::vector<uint8_t> ByteArray;
typedef std::vector<float> FloatArray;

static constexpr float PI = 3.14159265f;

struct thumbhash_encoder_struct {
    uint8_t* dst;
    size_t dst_len;
};

thumbhash_encoder thumbhash_encoder_create(void* buf, size_t buf_len)
{
    thumbhash_encoder e = new struct thumbhash_encoder_struct();
    if (!e) {
        return NULL;
    }
    memset(e, 0, sizeof(struct thumbhash_encoder_struct));
    e->dst = (uint8_t*)(buf);
    e->dst_len = buf_len;

    return e;
}

static std::tuple<float, FloatArray, float> encode_channel(const FloatArray& channel, size_t nx, size_t ny, size_t w, size_t h) {
    float dc = 0.0f;
    FloatArray ac;
    ac.reserve(nx * ny / 2);
    float scale = 0.0f;
    FloatArray fx(w, 0.0f);
    for (size_t cy = 0; cy < ny; ++cy) {
        size_t cx = 0;
        while (cx * ny < nx * (ny - cy)) {
            float f = 0.0f;
            for (size_t x = 0; x < w; ++x) {
                fx[x] = cos(PI / static_cast<float>(w) * static_cast<float>(cx) * (static_cast<float>(x) + 0.5f));
            }
            for (size_t y = 0; y < h; ++y) {
                float fy = cos(PI / static_cast<float>(h) * static_cast<float>(cy) * (static_cast<float>(y) + 0.5f));
                for (size_t x = 0; x < w; ++x) {
                    f += channel[x + y * w] * fx[x] * fy;
                }
            }
            f /= static_cast<float>(w * h);
            if (cx > 0 || cy > 0) {
                ac.push_back(f);
                scale = std::max(std::abs(f), scale);
            } else {
                dc = f;
            }
            cx += 1;
        }
    }
    if (scale > 0.0) {
        for (auto& ac_val : ac) {
            ac_val = 0.5f + 0.5f / scale * ac_val;
        }
    }
    return std::make_tuple(dc, ac, scale);
}

int thumbhash_encoder_encode(thumbhash_encoder e,
                             const opencv_mat opaque_frame)
{
    auto frame = static_cast<const cv::Mat*>(opaque_frame);

    // std::cout << "Matrix type: " << frame->type() << std::endl;
    // std::cout << "Number of channels: " << frame->channels() << std::endl;
    // std::cout << "Number of columns: " << frame->cols << std::endl;
    // std::cout << "Number of rows: " << frame->rows << std::endl;
    // std::cout << "Width step: " << frame->step << std::endl;
    // std::cout << "Depth: " << frame->depth() << std::endl;

    size_t w = frame->cols;
    size_t h = frame->rows;

    float avg_r = 0.0;
    float avg_g = 0.0;
    float avg_b = 0.0;
    float avg_a = 0.0;

    // Check the type of cv::Mat and adjust the pixel reading process accordingly
    if (frame->type() == CV_8UC3) {
        // 3 channels (BGR)
        for (int i = 0; i < h; ++i) {
            for (int j = 0; j < w; ++j) {
                const cv::Vec3b& pixel = frame->at<cv::Vec3b>(i, j);
                float alpha = 1.0f; // Assume fully opaque for CV_8UC3
                avg_b += (alpha / 255.0f) * static_cast<float>(pixel[0]); // B
                avg_g += (alpha / 255.0f) * static_cast<float>(pixel[1]); // G
                avg_r += (alpha / 255.0f) * static_cast<float>(pixel[2]); // R
                avg_a += alpha;
                // std::cout << "Pixel at (" << i * w + j << "): "
                //   << "R: " << static_cast<int>(pixel[2]) << ", "
                //   << "G: " << static_cast<int>(pixel[1]) << ", "
                //   << "B: " << static_cast<int>(pixel[0]) << ", "
                //   << "A: " << static_cast<int>(255) << std::endl;
            }
        }
    } else if (frame->type() == CV_8UC4) {
        // 4 channels (BGRA)
        for (int i = 0; i < h; ++i) {
            for (int j = 0; j < w; ++j) {
                const cv::Vec4b& pixel = frame->at<cv::Vec4b>(i, j);
                float alpha = static_cast<float>(pixel[3]) / 255.0f; // A
                avg_b += (alpha / 255.0f) * static_cast<float>(pixel[0]); // B
                avg_g += (alpha / 255.0f) * static_cast<float>(pixel[1]); // G
                avg_r += (alpha / 255.0f) * static_cast<float>(pixel[2]); // R
                avg_a += alpha;
                // std::cout << "Pixel at (" << i * w + j << "): "
                //   << "R: " << static_cast<int>(pixel[2]) << ", "
                //   << "G: " << static_cast<int>(pixel[1]) << ", "
                //   << "B: " << static_cast<int>(pixel[0]) << ", "
                //   << "A: " << static_cast<int>(pixel[3]) << std::endl;
            }
        }
    } else {
        return -1;
    }
    if (avg_a > 0.0f) {
        avg_r /= avg_a;
        avg_g /= avg_a;
        avg_b /= avg_a;
    }

    bool has_alpha = avg_a < static_cast<float>(w * h);
    size_t l_limit = has_alpha ? 5 : 7; // Use fewer luminance bits if there's alpha

    size_t lx = std::max(static_cast<size_t>(std::round(static_cast<float>(l_limit * w) / static_cast<float>(std::max(w, h)))), static_cast<size_t>(1));
    size_t ly = std::max(static_cast<size_t>(std::round(static_cast<float>(l_limit * h) / static_cast<float>(std::max(w, h)))), static_cast<size_t>(1));
    // std::cout << "lx: " << lx << ", ly: " << ly << std::endl;

    FloatArray l, p, q, a;
    l.reserve(w * h);
    p.reserve(w * h);
    q.reserve(w * h);
    a.reserve(w * h);

    if (frame->type() == CV_8UC3) {
        // 3 channels (BGR)
        for (int i = 0; i < h; ++i) {
            for (int j = 0; j < w; ++j) {
                const cv::Vec3b& pixel = frame->at<cv::Vec3b>(i, j);
                float alpha = 1.0f; // Assume fully opaque for CV_8UC3
                float b = avg_b * (1.0f - alpha) + (alpha / 255.0f) * static_cast<float>(pixel[0]); // B
                float g = avg_g * (1.0f - alpha) + (alpha / 255.0f) * static_cast<float>(pixel[1]); // G
                float r = avg_r * (1.0f - alpha) + (alpha / 255.0f) * static_cast<float>(pixel[2]); // R
                l.push_back((r + g + b) / 3.0f);
                p.push_back((r + g) / 2.0f - b);
                q.push_back(r - g);
                a.push_back(alpha);
            }
        }
    } else if (frame->type() == CV_8UC4) {
        // 4 channels (BGRA)
        for (int i = 0; i < h; ++i) {
            for (int j = 0; j < w; ++j) {
                const cv::Vec4b& pixel = frame->at<cv::Vec4b>(i, j);
                float alpha = static_cast<float>(pixel[3]) / 255.0f; // A
                float b = avg_b * (1.0f - alpha) + (alpha / 255.0f) * static_cast<float>(pixel[0]); // B
                float g = avg_g * (1.0f - alpha) + (alpha / 255.0f) * static_cast<float>(pixel[1]); // G
                float r = avg_r * (1.0f - alpha) + (alpha / 255.0f) * static_cast<float>(pixel[2]); // R
                l.push_back((r + g + b) / 3.0f);
                p.push_back((r + g) / 2.0f - b);
                q.push_back(r - g);
                a.push_back(alpha);
            }
        }
    } else {
        // Unsupported format
        return -1;
    }

    float l_dc, l_scale, p_dc, p_scale, q_dc, q_scale, a_dc, a_scale;
    FloatArray l_ac, p_ac, q_ac, a_ac;
    std::tie(l_dc, l_ac, l_scale) = encode_channel(l, std::max(lx, static_cast<size_t>(3)), std::max(ly, static_cast<size_t>(3)), w, h);
    std::tie(p_dc, p_ac, p_scale) = encode_channel(p, 3, 3, w, h);
    std::tie(q_dc, q_ac, q_scale) = encode_channel(q, 3, 3, w, h);
    if (has_alpha) {
        std::tie(a_dc, a_ac, a_scale) = encode_channel(a, 5, 5, w, h);
    } else {
        a_dc = 1.0f;
        a_scale = 1.0f;
    }

    bool is_landscape = w > h;
    uint32_t header24 = static_cast<uint32_t>(std::round(63.0f * l_dc))
                        | (static_cast<uint32_t>(std::round(31.5f + 31.5f * p_dc)) << 6)
                        | (static_cast<uint32_t>(std::round(31.5f + 31.5f * q_dc)) << 12)
                        | (static_cast<uint32_t>(std::round(31.0f * l_scale)) << 18)
                        | (has_alpha ? 1 << 23 : 0);
    uint16_t header16 = static_cast<uint16_t>(is_landscape ? ly : lx)
                        | (static_cast<uint16_t>(std::round(63.0f * p_scale)) << 3)
                        | (static_cast<uint16_t>(std::round(63.0f * q_scale)) << 9)
                        | (is_landscape ? 1 << 15 : 0);
    ByteArray hash;
    hash.reserve(25);
    hash.push_back(header24 & 255);
    hash.push_back((header24 >> 8) & 255);
    hash.push_back(header24 >> 16);
    hash.push_back(header16 & 255);
    hash.push_back(header16 >> 8);
    bool is_odd = false;
    if (has_alpha) {
        hash.push_back(static_cast<uint8_t>(std::round(15.0f * a_dc)) | (static_cast<uint8_t>(std::round(15.0f * a_scale)) << 4));
    }
    for (auto ac : {l_ac, p_ac, q_ac}) {
        for (float f : ac) {
            uint8_t u = static_cast<uint8_t>(std::round(15.0f * f));
            if (is_odd) {
                *hash.rbegin() |= u << 4;
            } else {
                hash.push_back(u);
            }
            is_odd = !is_odd;
        }
    }
    if (has_alpha) {
        for (float f : a_ac) {
            uint8_t u = static_cast<uint8_t>(std::round(15.0f * f));
            if (is_odd) {
                *hash.rbegin() |= u << 4;
            } else {
                hash.push_back(u);
            }
            is_odd = !is_odd;
        }
    }
    if (hash.size() <= e->dst_len) {
        // If the hash fits, copy the hash into the destination
        std::copy(hash.begin(), hash.end(), e->dst);
    } else {
        return -1;
    }
    return hash.size();
}

void thumbhash_encoder_release(thumbhash_encoder e)
{
    delete e;
}
