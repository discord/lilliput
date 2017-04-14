#include "test_precomp.hpp"
#include "test_intrin_utils.hpp"
#include <climits>

using namespace cv;

namespace cvtest { namespace hal {

template<typename T> static inline void EXPECT_COMPARE_EQ_(const T a, const T b);
template<> inline void EXPECT_COMPARE_EQ_<float>(const float a, const float b)
{
    EXPECT_FLOAT_EQ( a, b );
}

template<> inline void EXPECT_COMPARE_EQ_<double>(const double a, const double b)
{
    EXPECT_DOUBLE_EQ( a, b );
}

template<typename R> struct TheTest
{
    typedef typename R::lane_type LaneType;

    template <typename T1, typename T2>
    static inline void EXPECT_COMPARE_EQ(const T1 a, const T2 b)
    {
        EXPECT_COMPARE_EQ_<LaneType>((LaneType)a, (LaneType)b);
    }

    TheTest & test_loadstore()
    {
        AlignedData<R> data;
        AlignedData<R> out;

        // check if addresses are aligned and unaligned respectively
        EXPECT_EQ((size_t)0, (size_t)&data.a.d % 16);
        EXPECT_NE((size_t)0, (size_t)&data.u.d % 16);
        EXPECT_EQ((size_t)0, (size_t)&out.a.d % 16);
        EXPECT_NE((size_t)0, (size_t)&out.u.d % 16);

        // check some initialization methods
        R r1 = data.a;
        R r2 = v_load(data.u.d);
        R r3 = v_load_aligned(data.a.d);
        R r4(r2);
        EXPECT_EQ(data.a[0], r1.get0());
        EXPECT_EQ(data.u[0], r2.get0());
        EXPECT_EQ(data.a[0], r3.get0());
        EXPECT_EQ(data.u[0], r4.get0());

        // check some store methods
        out.u.clear();
        out.a.clear();
        v_store(out.u.d, r1);
        v_store_aligned(out.a.d, r2);
        EXPECT_EQ(data.a, out.a);
        EXPECT_EQ(data.u, out.u);

        // check more store methods
        Data<R> d, res(0);
        R r5 = d;
        v_store_high(res.mid(), r5);
        v_store_low(res.d, r5);
        EXPECT_EQ(d, res);

        // check halves load correctness
        res.clear();
        R r6 = v_load_halves(d.d, d.mid());
        v_store(res.d, r6);
        EXPECT_EQ(d, res);

        // zero, all
        Data<R> resZ = V_RegTrait128<LaneType>::zero();
        Data<R> resV = V_RegTrait128<LaneType>::all(8);
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ((LaneType)0, resZ[i]);
            EXPECT_EQ((LaneType)8, resV[i]);
        }

        // reinterpret_as
        v_uint8x16 vu8 = v_reinterpret_as_u8(r1); out.a.clear(); v_store((uchar*)out.a.d, vu8); EXPECT_EQ(data.a, out.a);
        v_int8x16 vs8 = v_reinterpret_as_s8(r1); out.a.clear(); v_store((schar*)out.a.d, vs8); EXPECT_EQ(data.a, out.a);
        v_uint16x8 vu16 = v_reinterpret_as_u16(r1); out.a.clear(); v_store((ushort*)out.a.d, vu16); EXPECT_EQ(data.a, out.a);
        v_int16x8 vs16 = v_reinterpret_as_s16(r1); out.a.clear(); v_store((short*)out.a.d, vs16); EXPECT_EQ(data.a, out.a);
        v_uint32x4 vu32 = v_reinterpret_as_u32(r1); out.a.clear(); v_store((unsigned*)out.a.d, vu32); EXPECT_EQ(data.a, out.a);
        v_int32x4 vs32 = v_reinterpret_as_s32(r1); out.a.clear(); v_store((int*)out.a.d, vs32); EXPECT_EQ(data.a, out.a);
        v_uint64x2 vu64 = v_reinterpret_as_u64(r1); out.a.clear(); v_store((uint64*)out.a.d, vu64); EXPECT_EQ(data.a, out.a);
        v_int64x2 vs64 = v_reinterpret_as_s64(r1); out.a.clear(); v_store((int64*)out.a.d, vs64); EXPECT_EQ(data.a, out.a);
        v_float32x4 vf32 = v_reinterpret_as_f32(r1); out.a.clear(); v_store((float*)out.a.d, vf32); EXPECT_EQ(data.a, out.a);
#if CV_SIMD128_64F
        v_float64x2 vf64 = v_reinterpret_as_f64(r1); out.a.clear(); v_store((double*)out.a.d, vf64); EXPECT_EQ(data.a, out.a);
#endif

        return *this;
    }

    TheTest & test_interleave()
    {
        Data<R> data1, data2, data3, data4;
        data2 += 20;
        data3 += 40;
        data4 += 60;


        R a = data1, b = data2, c = data3;
        R d = data1, e = data2, f = data3, g = data4;

        LaneType buf3[R::nlanes * 3];
        LaneType buf4[R::nlanes * 4];

        v_store_interleave(buf3, a, b, c);
        v_store_interleave(buf4, d, e, f, g);

        Data<R> z(0);
        a = b = c = d = e = f = g = z;

        v_load_deinterleave(buf3, a, b, c);
        v_load_deinterleave(buf4, d, e, f, g);

        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(data1, Data<R>(a));
            EXPECT_EQ(data2, Data<R>(b));
            EXPECT_EQ(data3, Data<R>(c));

            EXPECT_EQ(data1, Data<R>(d));
            EXPECT_EQ(data2, Data<R>(e));
            EXPECT_EQ(data3, Data<R>(f));
            EXPECT_EQ(data4, Data<R>(g));
        }

        return *this;
    }

    // float32x4 only
    TheTest & test_interleave_2channel()
    {
        Data<R> data1, data2;
        data2 += 20;

        R a = data1, b = data2;

        LaneType buf2[R::nlanes * 2];

        v_store_interleave(buf2, a, b);

        Data<R> z(0);
        a = b = z;

        v_load_deinterleave(buf2, a, b);

        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(data1, Data<R>(a));
            EXPECT_EQ(data2, Data<R>(b));
        }

        return *this;
    }

    // v_expand and v_load_expand
    TheTest & test_expand()
    {
        typedef typename V_RegTrait128<LaneType>::w_reg Rx2;
        Data<R> dataA;
        R a = dataA;

        Data<Rx2> resB = v_load_expand(dataA.d);

        Rx2 c, d;
        v_expand(a, c, d);

        Data<Rx2> resC = c, resD = d;
        const int n = Rx2::nlanes;
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ(dataA[i], resB[i]);
            EXPECT_EQ(dataA[i], resC[i]);
            EXPECT_EQ(dataA[i + n], resD[i]);
        }

        return *this;
    }

    TheTest & test_expand_q()
    {
        typedef typename V_RegTrait128<LaneType>::q_reg Rx4;
        Data<R> data;
        Data<Rx4> out = v_load_expand_q(data.d);
        const int n = Rx4::nlanes;
        for (int i = 0; i < n; ++i)
            EXPECT_EQ(data[i], out[i]);

        return *this;
    }

    TheTest & test_addsub()
    {
        Data<R> dataA, dataB;
        dataB.reverse();
        R a = dataA, b = dataB;

        Data<R> resC = a + b, resD = a - b;
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(saturate_cast<LaneType>(dataA[i] + dataB[i]), resC[i]);
            EXPECT_EQ(saturate_cast<LaneType>(dataA[i] - dataB[i]), resD[i]);
        }

        return *this;
    }

    TheTest & test_addsub_wrap()
    {
        Data<R> dataA, dataB;
        dataB.reverse();
        R a = dataA, b = dataB;

        Data<R> resC = v_add_wrap(a, b),
                resD = v_sub_wrap(a, b);
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ((LaneType)(dataA[i] + dataB[i]), resC[i]);
            EXPECT_EQ((LaneType)(dataA[i] - dataB[i]), resD[i]);
        }
        return *this;
    }

    TheTest & test_mul()
    {
        Data<R> dataA, dataB;
        dataB.reverse();
        R a = dataA, b = dataB;

        Data<R> resC = a * b;
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(dataA[i] * dataB[i], resC[i]);
        }

        return *this;
    }

    TheTest & test_div()
    {
        Data<R> dataA, dataB;
        dataB.reverse();
        R a = dataA, b = dataB;

        Data<R> resC = a / b;
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(dataA[i] / dataB[i], resC[i]);
        }

        return *this;
    }

    TheTest & test_mul_expand()
    {
        typedef typename V_RegTrait128<LaneType>::w_reg Rx2;
        Data<R> dataA, dataB(2);
        R a = dataA, b = dataB;
        Rx2 c, d;

        v_mul_expand(a, b, c, d);

        Data<Rx2> resC = c, resD = d;
        const int n = R::nlanes / 2;
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ((typename Rx2::lane_type)dataA[i] * dataB[i], resC[i]);
            EXPECT_EQ((typename Rx2::lane_type)dataA[i + n] * dataB[i + n], resD[i]);
        }

        return *this;
    }

    TheTest & test_abs()
    {
        typedef typename V_RegTrait128<LaneType>::u_reg Ru;
        typedef typename Ru::lane_type u_type;
        Data<R> dataA, dataB(10);
        R a = dataA, b = dataB;
        a = a - b;

        Data<Ru> resC = v_abs(a);

        for (int i = 0; i < Ru::nlanes; ++i)
        {
            EXPECT_EQ((u_type)std::abs(dataA[i] - dataB[i]), resC[i]);
        }

        return *this;
    }

    template <int s>
    TheTest & test_shift()
    {
        Data<R> dataA;
        R a = dataA;

        Data<R> resB = a << s, resC = v_shl<s>(a), resD = a >> s, resE = v_shr<s>(a);
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(dataA[i] << s, resB[i]);
            EXPECT_EQ(dataA[i] << s, resC[i]);
            EXPECT_EQ(dataA[i] >> s, resD[i]);
            EXPECT_EQ(dataA[i] >> s, resE[i]);
        }
        return *this;
    }

    TheTest & test_cmp()
    {
        Data<R> dataA, dataB;
        dataB.reverse();
        dataB += 1;
        R a = dataA, b = dataB;

        Data<R> resC = (a == b);
        Data<R> resD = (a != b);
        Data<R> resE = (a > b);
        Data<R> resF = (a >= b);
        Data<R> resG = (a < b);
        Data<R> resH = (a <= b);

        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(dataA[i] == dataB[i], resC[i] != 0);
            EXPECT_EQ(dataA[i] != dataB[i], resD[i] != 0);
            EXPECT_EQ(dataA[i] >  dataB[i], resE[i] != 0);
            EXPECT_EQ(dataA[i] >= dataB[i], resF[i] != 0);
            EXPECT_EQ(dataA[i] <  dataB[i], resG[i] != 0);
            EXPECT_EQ(dataA[i] <= dataB[i], resH[i] != 0);
        }
        return *this;
    }

    TheTest & test_dot_prod()
    {
        typedef typename V_RegTrait128<LaneType>::w_reg Rx2;
        Data<R> dataA, dataB(2);
        R a = dataA, b = dataB;

        Data<Rx2> res = v_dotprod(a, b);

        const int n = R::nlanes / 2;
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ(dataA[i*2] * dataB[i*2] + dataA[i*2 + 1] * dataB[i*2 + 1], res[i]);
        }
        return *this;
    }

    TheTest & test_logic()
    {
        Data<R> dataA, dataB(2);
        R a = dataA, b = dataB;

        Data<R> resC = a & b, resD = a | b, resE = a ^ b, resF = ~a;
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(dataA[i] & dataB[i], resC[i]);
            EXPECT_EQ(dataA[i] | dataB[i], resD[i]);
            EXPECT_EQ(dataA[i] ^ dataB[i], resE[i]);
            EXPECT_EQ((LaneType)~dataA[i], resF[i]);
        }

        return *this;
    }

    TheTest & test_sqrt_abs()
    {
        Data<R> dataA, dataD;
        dataD *= -1.0;
        R a = dataA, d = dataD;

        Data<R> resB = v_sqrt(a), resC = v_invsqrt(a), resE = v_abs(d);
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_COMPARE_EQ((float)std::sqrt(dataA[i]), (float)resB[i]);
            EXPECT_COMPARE_EQ(1/(float)std::sqrt(dataA[i]), (float)resC[i]);
            EXPECT_COMPARE_EQ((float)abs(dataA[i]), (float)resE[i]);
        }

        return *this;
    }

    TheTest & test_min_max()
    {
        Data<R> dataA, dataB;
        dataB.reverse();
        R a = dataA, b = dataB;

        Data<R> resC = v_min(a, b), resD = v_max(a, b);
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(std::min(dataA[i], dataB[i]), resC[i]);
            EXPECT_EQ(std::max(dataA[i], dataB[i]), resD[i]);
        }

        return *this;
    }

    TheTest & test_absdiff()
    {
        typedef typename V_RegTrait128<LaneType>::u_reg Ru;
        typedef typename Ru::lane_type u_type;
        Data<R> dataA(std::numeric_limits<LaneType>::max()),
                dataB(std::numeric_limits<LaneType>::min());
        dataA[0] = (LaneType)-1;
        dataB[0] = 1;
        dataA[1] = 2;
        dataB[1] = (LaneType)-2;
        R a = dataA, b = dataB;
        Data<Ru> resC = v_absdiff(a, b);
        const u_type mask = std::numeric_limits<LaneType>::is_signed ? (u_type)(1 << (sizeof(u_type)*8 - 1)) : 0;
        for (int i = 0; i < Ru::nlanes; ++i)
        {
            u_type uA = dataA[i] ^ mask;
            u_type uB = dataB[i] ^ mask;
            EXPECT_EQ(uA > uB ? uA - uB : uB - uA, resC[i]);
        }
        return *this;
    }

    TheTest & test_float_absdiff()
    {
        Data<R> dataA(std::numeric_limits<LaneType>::max()),
                dataB(std::numeric_limits<LaneType>::min());
        dataA[0] = -1;
        dataB[0] = 1;
        dataA[1] = 2;
        dataB[1] = -2;
        R a = dataA, b = dataB;
        Data<R> resC = v_absdiff(a, b);
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(dataA[i] > dataB[i] ? dataA[i] - dataB[i] : dataB[i] - dataA[i], resC[i]);
        }
        return *this;
    }

    TheTest & test_reduce()
    {
        Data<R> dataA;
        R a = dataA;
        EXPECT_EQ((LaneType)1, v_reduce_min(a));
        EXPECT_EQ((LaneType)R::nlanes, v_reduce_max(a));
        EXPECT_EQ((LaneType)((1 + R::nlanes)*R::nlanes/2), v_reduce_sum(a));
        return *this;
    }

    TheTest & test_mask()
    {
        Data<R> dataA, dataB, dataC, dataD(1), dataE(2);
        dataA[1] *= (LaneType)-1;
        dataC *= (LaneType)-1;
        R a = dataA, b = dataB, c = dataC, d = dataD, e = dataE;

        int m = v_signmask(a);
        EXPECT_EQ(2, m);

        EXPECT_EQ(false, v_check_all(a));
        EXPECT_EQ(false, v_check_all(b));
        EXPECT_EQ(true, v_check_all(c));

        EXPECT_EQ(true, v_check_any(a));
        EXPECT_EQ(false, v_check_any(b));
        EXPECT_EQ(true, v_check_any(c));

        typedef V_TypeTraits<LaneType> Traits;
        typedef typename Traits::int_type int_type;

        R f = v_select(b, d, e);
        Data<R> resF = f;
        for (int i = 0; i < R::nlanes; ++i)
        {
            int_type m2 = Traits::reinterpret_int(dataB[i]);
            EXPECT_EQ((Traits::reinterpret_int(dataD[i]) & m2)
                    | (Traits::reinterpret_int(dataE[i]) & ~m2),
                      Traits::reinterpret_int(resF[i]));
        }

        return *this;
    }

    template <int s>
    TheTest & test_pack()
    {
        typedef typename V_RegTrait128<LaneType>::w_reg Rx2;
        typedef typename Rx2::lane_type w_type;
        Data<Rx2> dataA, dataB;
        dataA += std::numeric_limits<LaneType>::is_signed ? -10 : 10;
        dataB *= 10;
        Rx2 a = dataA, b = dataB;

        Data<R> resC = v_pack(a, b);
        Data<R> resD = v_rshr_pack<s>(a, b);

        Data<R> resE(0);
        v_pack_store(resE.d, b);

        Data<R> resF(0);
        v_rshr_pack_store<s>(resF.d, b);

        const int n = Rx2::nlanes;
        const w_type add = (w_type)1 << (s - 1);
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ(saturate_cast<LaneType>(dataA[i]), resC[i]);
            EXPECT_EQ(saturate_cast<LaneType>(dataB[i]), resC[i + n]);
            EXPECT_EQ(saturate_cast<LaneType>((dataA[i] + add) >> s), resD[i]);
            EXPECT_EQ(saturate_cast<LaneType>((dataB[i] + add) >> s), resD[i + n]);
            EXPECT_EQ(saturate_cast<LaneType>(dataB[i]), resE[i]);
            EXPECT_EQ((LaneType)0, resE[i + n]);
            EXPECT_EQ(saturate_cast<LaneType>((dataB[i] + add) >> s), resF[i]);
            EXPECT_EQ((LaneType)0, resF[i + n]);
        }
        return *this;
    }

    template <int s>
    TheTest & test_pack_u()
    {
        typedef typename V_TypeTraits<LaneType>::w_type LaneType_w;
        typedef typename V_RegTrait128<LaneType_w>::int_reg Ri2;
        typedef typename Ri2::lane_type w_type;

        Data<Ri2> dataA, dataB;
        dataA += -10;
        dataB *= 10;
        Ri2 a = dataA, b = dataB;

        Data<R> resC = v_pack_u(a, b);
        Data<R> resD = v_rshr_pack_u<s>(a, b);

        Data<R> resE(0);
        v_pack_u_store(resE.d, b);

        Data<R> resF(0);
        v_rshr_pack_u_store<s>(resF.d, b);

        const int n = Ri2::nlanes;
        const w_type add = (w_type)1 << (s - 1);
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ(saturate_cast<LaneType>(dataA[i]), resC[i]);
            EXPECT_EQ(saturate_cast<LaneType>(dataB[i]), resC[i + n]);
            EXPECT_EQ(saturate_cast<LaneType>((dataA[i] + add) >> s), resD[i]);
            EXPECT_EQ(saturate_cast<LaneType>((dataB[i] + add) >> s), resD[i + n]);
            EXPECT_EQ(saturate_cast<LaneType>(dataB[i]), resE[i]);
            EXPECT_EQ((LaneType)0, resE[i + n]);
            EXPECT_EQ(saturate_cast<LaneType>((dataB[i] + add) >> s), resF[i]);
            EXPECT_EQ((LaneType)0, resF[i + n]);
        }
        return *this;
    }

    TheTest & test_unpack()
    {
        Data<R> dataA, dataB;
        dataB *= 10;
        R a = dataA, b = dataB;

        R c, d, e, f, lo, hi;
        v_zip(a, b, c, d);
        v_recombine(a, b, e, f);
        lo = v_combine_low(a, b);
        hi = v_combine_high(a, b);

        Data<R> resC = c, resD = d, resE = e, resF = f, resLo = lo, resHi = hi;

        const int n = R::nlanes/2;
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ(dataA[i], resC[i*2]);
            EXPECT_EQ(dataB[i], resC[i*2+1]);
            EXPECT_EQ(dataA[i+n], resD[i*2]);
            EXPECT_EQ(dataB[i+n], resD[i*2+1]);

            EXPECT_EQ(dataA[i], resE[i]);
            EXPECT_EQ(dataB[i], resE[i+n]);
            EXPECT_EQ(dataA[i+n], resF[i]);
            EXPECT_EQ(dataB[i+n], resF[i+n]);

            EXPECT_EQ(dataA[i], resLo[i]);
            EXPECT_EQ(dataB[i], resLo[i+n]);
            EXPECT_EQ(dataA[i+n], resHi[i]);
            EXPECT_EQ(dataB[i+n], resHi[i+n]);
        }

        return *this;
    }

    template<int s>
    TheTest & test_extract()
    {
        Data<R> dataA, dataB;
        dataB *= 10;
        R a = dataA, b = dataB;

        Data<R> resC = v_extract<s>(a, b);

        for (int i = 0; i < R::nlanes; ++i)
        {
            if (i + s >= R::nlanes)
                EXPECT_EQ(dataB[i - R::nlanes + s], resC[i]);
            else
                EXPECT_EQ(dataA[i + s], resC[i]);
        }

        return *this;
    }

    TheTest & test_float_math()
    {
        typedef typename V_RegTrait128<LaneType>::int_reg Ri;
        Data<R> data1, data2, data3;
        data1 *= 1.1;
        data2 += 10;
        R a1 = data1, a2 = data2, a3 = data3;

        Data<Ri> resB = v_round(a1),
                 resC = v_trunc(a1),
                 resD = v_floor(a1),
                 resE = v_ceil(a1);

        Data<R> resF = v_magnitude(a1, a2),
                resG = v_sqr_magnitude(a1, a2),
                resH = v_muladd(a1, a2, a3);

        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(cvRound(data1[i]), resB[i]);
            EXPECT_EQ((typename Ri::lane_type)data1[i], resC[i]);
            EXPECT_EQ(cvFloor(data1[i]), resD[i]);
            EXPECT_EQ(cvCeil(data1[i]), resE[i]);

            EXPECT_COMPARE_EQ(std::sqrt(data1[i]*data1[i] + data2[i]*data2[i]), resF[i]);
            EXPECT_COMPARE_EQ(data1[i]*data1[i] + data2[i]*data2[i], resG[i]);
            EXPECT_COMPARE_EQ(data1[i]*data2[i] + data3[i], resH[i]);
        }

        return *this;
    }

    TheTest & test_float_cvt32()
    {
        typedef v_float32x4 Rt;
        Data<R> dataA;
        dataA *= 1.1;
        R a = dataA;
        Rt b = v_cvt_f32(a);
        Data<Rt> resB = b;
        int n = std::min<int>(Rt::nlanes, R::nlanes);
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ((typename Rt::lane_type)dataA[i], resB[i]);
        }
        return *this;
    }

    TheTest & test_float_cvt64()
    {
#if CV_SIMD128_64F
        typedef v_float64x2 Rt;
        Data<R> dataA;
        dataA *= 1.1;
        R a = dataA;
        Rt b = v_cvt_f64(a);
        Rt c = v_cvt_f64_high(a);
        Data<Rt> resB = b;
        Data<Rt> resC = c;
        int n = std::min<int>(Rt::nlanes, R::nlanes);
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ((typename Rt::lane_type)dataA[i], resB[i]);
        }
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ((typename Rt::lane_type)dataA[i+n], resC[i]);
        }
#endif
        return *this;
    }

    TheTest & test_matmul()
    {
        Data<R> dataV, dataA, dataB, dataC, dataD;
        dataB.reverse();
        dataC += 2;
        dataD *= 0.3;
        R v = dataV, a = dataA, b = dataB, c = dataC, d = dataD;

        Data<R> res = v_matmul(v, a, b, c, d);
        for (int i = 0; i < R::nlanes; ++i)
        {
            LaneType val = dataV[0] * dataA[i]
                                      + dataV[1] * dataB[i]
                                      + dataV[2] * dataC[i]
                                      + dataV[3] * dataD[i];
            EXPECT_DOUBLE_EQ(val, res[i]);
        }
        return *this;
    }

    TheTest & test_transpose()
    {
        Data<R> dataA, dataB, dataC, dataD;
        dataB *= 5;
        dataC *= 10;
        dataD *= 15;
        R a = dataA, b = dataB, c = dataC, d = dataD;
        R e, f, g, h;
        v_transpose4x4(a, b, c, d,
                       e, f, g, h);

        Data<R> res[4] = {e, f, g, h};
        for (int i = 0; i < R::nlanes; ++i)
        {
            EXPECT_EQ(dataA[i], res[i][0]);
            EXPECT_EQ(dataB[i], res[i][1]);
            EXPECT_EQ(dataC[i], res[i][2]);
            EXPECT_EQ(dataD[i], res[i][3]);
        }
        return *this;
    }

    TheTest & test_loadstore_fp16()
    {
#if CV_FP16
        AlignedData<R> data;
        AlignedData<R> out;

        if(checkHardwareSupport(CV_CPU_FP16))
        {
            // check if addresses are aligned and unaligned respectively
            EXPECT_EQ((size_t)0, (size_t)&data.a.d % 16);
            EXPECT_NE((size_t)0, (size_t)&data.u.d % 16);
            EXPECT_EQ((size_t)0, (size_t)&out.a.d % 16);
            EXPECT_NE((size_t)0, (size_t)&out.u.d % 16);

            // check some initialization methods
            R r1 = data.u;
            R r2 = v_load_f16(data.a.d);
            R r3(r2);
            EXPECT_EQ(data.u[0], r1.get0());
            EXPECT_EQ(data.a[0], r2.get0());
            EXPECT_EQ(data.a[0], r3.get0());

            // check some store methods
            out.a.clear();
            v_store_f16(out.a.d, r1);
            EXPECT_EQ(data.a, out.a);
        }

        return *this;
#endif
    }

    TheTest & test_float_cvt_fp16()
    {
#if CV_FP16
        AlignedData<v_float32x4> data;

        if(checkHardwareSupport(CV_CPU_FP16))
        {
            // check conversion
            v_float32x4 r1 = v_load(data.a.d);
            v_float16x4 r2 = v_cvt_f16(r1);
            v_float32x4 r3 = v_cvt_f32(r2);
            EXPECT_EQ(0x3c00, r2.get0());
            EXPECT_EQ(r3.get0(), r1.get0());
        }

        return *this;
#endif
    }

};


//=============  8-bit integer =====================================================================

TEST(hal_intrin, uint8x16) {
    TheTest<v_uint8x16>()
        .test_loadstore()
        .test_interleave()
        .test_expand()
        .test_expand_q()
        .test_addsub()
        .test_addsub_wrap()
        .test_cmp()
        .test_logic()
        .test_min_max()
        .test_absdiff()
        .test_mask()
        .test_pack<1>().test_pack<2>().test_pack<3>().test_pack<8>()
        .test_pack_u<1>().test_pack_u<2>().test_pack_u<3>().test_pack_u<8>()
        .test_unpack()
        .test_extract<0>().test_extract<1>().test_extract<8>().test_extract<15>()
        ;
}

TEST(hal_intrin, int8x16) {
    TheTest<v_int8x16>()
        .test_loadstore()
        .test_interleave()
        .test_expand()
        .test_expand_q()
        .test_addsub()
        .test_addsub_wrap()
        .test_cmp()
        .test_logic()
        .test_min_max()
        .test_absdiff()
        .test_abs()
        .test_mask()
        .test_pack<1>().test_pack<2>().test_pack<3>().test_pack<8>()
        .test_unpack()
        .test_extract<0>().test_extract<1>().test_extract<8>().test_extract<15>()
        ;
}

//============= 16-bit integer =====================================================================

TEST(hal_intrin, uint16x8) {
    TheTest<v_uint16x8>()
        .test_loadstore()
        .test_interleave()
        .test_expand()
        .test_addsub()
        .test_addsub_wrap()
        .test_mul()
        .test_mul_expand()
        .test_cmp()
        .test_shift<1>()
        .test_shift<8>()
        .test_logic()
        .test_min_max()
        .test_absdiff()
        .test_reduce()
        .test_mask()
        .test_pack<1>().test_pack<2>().test_pack<7>().test_pack<16>()
        .test_pack_u<1>().test_pack_u<2>().test_pack_u<7>().test_pack_u<16>()
        .test_unpack()
        .test_extract<0>().test_extract<1>().test_extract<4>().test_extract<7>()
        ;
}

TEST(hal_intrin, int16x8) {
    TheTest<v_int16x8>()
        .test_loadstore()
        .test_interleave()
        .test_expand()
        .test_addsub()
        .test_addsub_wrap()
        .test_mul()
        .test_mul_expand()
        .test_cmp()
        .test_shift<1>()
        .test_shift<8>()
        .test_dot_prod()
        .test_logic()
        .test_min_max()
        .test_absdiff()
        .test_abs()
        .test_reduce()
        .test_mask()
        .test_pack<1>().test_pack<2>().test_pack<7>().test_pack<16>()
        .test_unpack()
        .test_extract<0>().test_extract<1>().test_extract<4>().test_extract<7>()
        ;
}

//============= 32-bit integer =====================================================================

TEST(hal_intrin, uint32x4) {
    TheTest<v_uint32x4>()
        .test_loadstore()
        .test_interleave()
        .test_expand()
        .test_addsub()
        .test_mul()
        .test_mul_expand()
        .test_cmp()
        .test_shift<1>()
        .test_shift<8>()
        .test_logic()
        .test_min_max()
        .test_absdiff()
        .test_reduce()
        .test_mask()
        .test_pack<1>().test_pack<2>().test_pack<15>().test_pack<32>()
        .test_unpack()
        .test_extract<0>().test_extract<1>().test_extract<2>().test_extract<3>()
        .test_transpose()
        ;
}

TEST(hal_intrin, int32x4) {
    TheTest<v_int32x4>()
        .test_loadstore()
        .test_interleave()
        .test_expand()
        .test_addsub()
        .test_mul()
        .test_abs()
        .test_cmp()
        .test_shift<1>().test_shift<8>()
        .test_logic()
        .test_min_max()
        .test_absdiff()
        .test_reduce()
        .test_mask()
        .test_pack<1>().test_pack<2>().test_pack<15>().test_pack<32>()
        .test_unpack()
        .test_extract<0>().test_extract<1>().test_extract<2>().test_extract<3>()
        .test_float_cvt32()
        .test_float_cvt64()
        .test_transpose()
        ;
}

//============= 64-bit integer =====================================================================

TEST(hal_intrin, uint64x2) {
    TheTest<v_uint64x2>()
        .test_loadstore()
        .test_addsub()
        .test_shift<1>().test_shift<8>()
        .test_logic()
        .test_extract<0>().test_extract<1>()
        ;
}

TEST(hal_intrin, int64x2) {
    TheTest<v_int64x2>()
        .test_loadstore()
        .test_addsub()
        .test_shift<1>().test_shift<8>()
        .test_logic()
        .test_extract<0>().test_extract<1>()
        ;
}

//============= Floating point =====================================================================

TEST(hal_intrin, float32x4) {
    TheTest<v_float32x4>()
        .test_loadstore()
        .test_interleave()
        .test_interleave_2channel()
        .test_addsub()
        .test_mul()
        .test_div()
        .test_cmp()
        .test_sqrt_abs()
        .test_min_max()
        .test_float_absdiff()
        .test_reduce()
        .test_mask()
        .test_unpack()
        .test_float_math()
        .test_float_cvt64()
        .test_matmul()
        .test_transpose()
        ;
}

#if CV_SIMD128_64F
TEST(hal_intrin, float64x2) {
    TheTest<v_float64x2>()
        .test_loadstore()
        .test_addsub()
        .test_mul()
        .test_div()
        .test_cmp()
        .test_sqrt_abs()
        .test_min_max()
        .test_float_absdiff()
        .test_mask()
        .test_unpack()
        .test_float_math()
        .test_float_cvt32()
        ;
}
#endif

#if CV_FP16
TEST(hal_intrin, float16x4) {
    TheTest<v_float16x4>()
        .test_loadstore_fp16()
        .test_float_cvt_fp16()
        ;
}
#endif

};

};
