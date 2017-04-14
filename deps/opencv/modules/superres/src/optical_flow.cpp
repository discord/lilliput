/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "precomp.hpp"
#include "opencv2/core/opencl/ocl_defs.hpp"

using namespace cv;
using namespace cv::cuda;
using namespace cv::superres;
using namespace cv::superres::detail;

///////////////////////////////////////////////////////////////////
// CpuOpticalFlow

namespace
{
    class CpuOpticalFlow : public virtual cv::superres::DenseOpticalFlowExt
    {
    public:
        explicit CpuOpticalFlow(int work_type);

        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

    protected:
        virtual void impl(InputArray input0, InputArray input1, OutputArray dst) = 0;

    private:
#ifdef HAVE_OPENCL
        bool ocl_calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
#endif

        int work_type_;

        // Mat
        Mat buf_[6];
        Mat flow_;
        Mat flows_[2];

        // UMat
        UMat ubuf_[6];
        UMat uflow_;
        std::vector<UMat> uflows_;
    };

    CpuOpticalFlow::CpuOpticalFlow(int work_type) :
        work_type_(work_type)
    {
    }

#ifdef HAVE_OPENCL
    bool CpuOpticalFlow::ocl_calc(InputArray _frame0, InputArray _frame1, OutputArray _flow1, OutputArray _flow2)
    {
        UMat frame0 = arrGetUMat(_frame0, ubuf_[0]);
        UMat frame1 = arrGetUMat(_frame1, ubuf_[1]);

        CV_Assert( frame1.type() == frame0.type() );
        CV_Assert( frame1.size() == frame0.size() );

        UMat input0 = convertToType(frame0, work_type_, ubuf_[2], ubuf_[3]);
        UMat input1 = convertToType(frame1, work_type_, ubuf_[4], ubuf_[5]);

        if (!_flow2.needed())
        {
            impl(input0, input1, _flow1);
            return true;
        }

        impl(input0, input1, uflow_);

        if (!_flow2.needed())
            arrCopy(uflow_, _flow1);
        else
        {
            split(uflow_, uflows_);

            arrCopy(uflows_[0], _flow1);
            arrCopy(uflows_[1], _flow2);
        }

        return true;
    }
#endif

    void CpuOpticalFlow::calc(InputArray _frame0, InputArray _frame1, OutputArray _flow1, OutputArray _flow2)
    {
        CV_INSTRUMENT_REGION()

        CV_OCL_RUN(_flow1.isUMat() && (_flow2.isUMat() || !_flow2.needed()),
                   ocl_calc(_frame0, _frame1, _flow1, _flow2))

        Mat frame0 = arrGetMat(_frame0, buf_[0]);
        Mat frame1 = arrGetMat(_frame1, buf_[1]);

        CV_Assert( frame1.type() == frame0.type() );
        CV_Assert( frame1.size() == frame0.size() );

        Mat input0 = convertToType(frame0, work_type_, buf_[2], buf_[3]);
        Mat input1 = convertToType(frame1, work_type_, buf_[4], buf_[5]);

        if (!_flow2.needed() && _flow1.kind() < _InputArray::OPENGL_BUFFER)
        {
            impl(input0, input1, _flow1);
            return;
        }

        impl(input0, input1, flow_);

        if (!_flow2.needed())
            arrCopy(flow_, _flow1);
        else
        {
            split(flow_, flows_);

            arrCopy(flows_[0], _flow1);
            arrCopy(flows_[1], _flow2);
        }
    }

    void CpuOpticalFlow::collectGarbage()
    {
        // Mat
        for (int i = 0; i < 6; ++i)
            buf_[i].release();
        flow_.release();
        flows_[0].release();
        flows_[1].release();

        // UMat
        for (int i = 0; i < 6; ++i)
            ubuf_[i].release();
        uflow_.release();
        uflows_[0].release();
        uflows_[1].release();
    }
}

///////////////////////////////////////////////////////////////////
// Farneback

namespace
{
    class Farneback : public CpuOpticalFlow, public cv::superres::FarnebackOpticalFlow
    {
    public:
        Farneback();
        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

        CV_IMPL_PROPERTY(double, PyrScale, pyrScale_)
        CV_IMPL_PROPERTY(int, LevelsNumber, numLevels_)
        CV_IMPL_PROPERTY(int, WindowSize, winSize_)
        CV_IMPL_PROPERTY(int, Iterations, numIters_)
        CV_IMPL_PROPERTY(int, PolyN, polyN_)
        CV_IMPL_PROPERTY(double, PolySigma, polySigma_)
        CV_IMPL_PROPERTY(int, Flags, flags_)

    protected:
        void impl(InputArray input0, InputArray input1, OutputArray dst);

    private:
        double pyrScale_;
        int numLevels_;
        int winSize_;
        int numIters_;
        int polyN_;
        double polySigma_;
        int flags_;
    };

    Farneback::Farneback() : CpuOpticalFlow(CV_8UC1)
    {
        pyrScale_ = 0.5;
        numLevels_ = 5;
        winSize_ = 13;
        numIters_ = 10;
        polyN_ = 5;
        polySigma_ = 1.1;
        flags_ = 0;
    }

    void Farneback::calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2)
    {
        CV_INSTRUMENT_REGION()

        CpuOpticalFlow::calc(frame0, frame1, flow1, flow2);
    }

    void Farneback::collectGarbage()
    {
        CpuOpticalFlow::collectGarbage();
    }

    void Farneback::impl(InputArray input0, InputArray input1, OutputArray dst)
    {
        calcOpticalFlowFarneback(input0, input1, InputOutputArray(dst), pyrScale_,
                                 numLevels_, winSize_, numIters_,
                                 polyN_, polySigma_, flags_);
    }
}

Ptr<cv::superres::FarnebackOpticalFlow> cv::superres::createOptFlow_Farneback()
{
    return makePtr<Farneback>();
}

///////////////////////////////////////////////////////////////////
// Simple

/*
namespace
{
    class Simple : public CpuOpticalFlow
    {
    public:
        AlgorithmInfo* info() const;

        Simple();

    protected:
        void impl(InputArray input0, InputArray input1, OutputArray dst);

    private:
        int layers_;
        int averagingBlockSize_;
        int maxFlow_;
        double sigmaDist_;
        double sigmaColor_;
        int postProcessWindow_;
        double sigmaDistFix_;
        double sigmaColorFix_;
        double occThr_;
        int upscaleAveragingRadius_;
        double upscaleSigmaDist_;
        double upscaleSigmaColor_;
        double speedUpThr_;
    };

    CV_INIT_ALGORITHM(Simple, "DenseOpticalFlowExt.Simple",
                      obj.info()->addParam(obj, "layers", obj.layers_);
                      obj.info()->addParam(obj, "averagingBlockSize", obj.averagingBlockSize_);
                      obj.info()->addParam(obj, "maxFlow", obj.maxFlow_);
                      obj.info()->addParam(obj, "sigmaDist", obj.sigmaDist_);
                      obj.info()->addParam(obj, "sigmaColor", obj.sigmaColor_);
                      obj.info()->addParam(obj, "postProcessWindow", obj.postProcessWindow_);
                      obj.info()->addParam(obj, "sigmaDistFix", obj.sigmaDistFix_);
                      obj.info()->addParam(obj, "sigmaColorFix", obj.sigmaColorFix_);
                      obj.info()->addParam(obj, "occThr", obj.occThr_);
                      obj.info()->addParam(obj, "upscaleAveragingRadius", obj.upscaleAveragingRadius_);
                      obj.info()->addParam(obj, "upscaleSigmaDist", obj.upscaleSigmaDist_);
                      obj.info()->addParam(obj, "upscaleSigmaColor", obj.upscaleSigmaColor_);
                      obj.info()->addParam(obj, "speedUpThr", obj.speedUpThr_))

    Simple::Simple() : CpuOpticalFlow(CV_8UC3)
    {
        layers_ = 3;
        averagingBlockSize_ = 2;
        maxFlow_ = 4;
        sigmaDist_ = 4.1;
        sigmaColor_ = 25.5;
        postProcessWindow_ = 18;
        sigmaDistFix_ = 55.0;
        sigmaColorFix_ = 25.5;
        occThr_ = 0.35;
        upscaleAveragingRadius_ = 18;
        upscaleSigmaDist_ = 55.0;
        upscaleSigmaColor_ = 25.5;
        speedUpThr_ = 10;
    }

    void Simple::impl(InputArray _input0, InputArray _input1, OutputArray _dst)
    {
        calcOpticalFlowSF(_input0, _input1, _dst,
                          layers_,
                          averagingBlockSize_,
                          maxFlow_,
                          sigmaDist_,
                          sigmaColor_,
                          postProcessWindow_,
                          sigmaDistFix_,
                          sigmaColorFix_,
                          occThr_,
                          upscaleAveragingRadius_,
                          upscaleSigmaDist_,
                          upscaleSigmaColor_,
                          speedUpThr_);
    }
}

Ptr<DenseOpticalFlowExt> cv::superres::createOptFlow_Simple()
{
    return makePtr<Simple>();
}*/

///////////////////////////////////////////////////////////////////
// DualTVL1

namespace
{
    class DualTVL1 : public CpuOpticalFlow, public virtual cv::superres::DualTVL1OpticalFlow
    {
    public:
        DualTVL1();
        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

        CV_WRAP_SAME_PROPERTY(double, Tau, (*alg_))
        CV_WRAP_SAME_PROPERTY(double, Lambda, (*alg_))
        CV_WRAP_SAME_PROPERTY(double, Theta, (*alg_))
        CV_WRAP_SAME_PROPERTY(int, ScalesNumber, (*alg_))
        CV_WRAP_SAME_PROPERTY(int, WarpingsNumber, (*alg_))
        CV_WRAP_SAME_PROPERTY(double, Epsilon, (*alg_))
        CV_WRAP_PROPERTY(int, Iterations, OuterIterations, (*alg_))
        CV_WRAP_SAME_PROPERTY(bool, UseInitialFlow, (*alg_))

    protected:
        void impl(InputArray input0, InputArray input1, OutputArray dst);

    private:
        Ptr<cv::DualTVL1OpticalFlow> alg_;
    };

    DualTVL1::DualTVL1() : CpuOpticalFlow(CV_8UC1)
    {
        alg_ = cv::createOptFlow_DualTVL1();
    }

    void DualTVL1::calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2)
    {
        CV_INSTRUMENT_REGION()

        CpuOpticalFlow::calc(frame0, frame1, flow1, flow2);
    }

    void DualTVL1::impl(InputArray input0, InputArray input1, OutputArray dst)
    {
        alg_->calc(input0, input1, (InputOutputArray)dst);
    }

    void DualTVL1::collectGarbage()
    {
        alg_->collectGarbage();
        CpuOpticalFlow::collectGarbage();
    }
}

Ptr<cv::superres::DualTVL1OpticalFlow> cv::superres::createOptFlow_DualTVL1()
{
    return makePtr<DualTVL1>();
}

///////////////////////////////////////////////////////////////////
// GpuOpticalFlow

#ifndef HAVE_OPENCV_CUDAOPTFLOW

Ptr<cv::superres::FarnebackOpticalFlow> cv::superres::createOptFlow_Farneback_CUDA()
{
    CV_Error(cv::Error::StsNotImplemented, "The called functionality is disabled for current build or platform");
    return Ptr<cv::superres::FarnebackOpticalFlow>();
}

Ptr<cv::superres::DualTVL1OpticalFlow> cv::superres::createOptFlow_DualTVL1_CUDA()
{
    CV_Error(cv::Error::StsNotImplemented, "The called functionality is disabled for current build or platform");
    return Ptr<cv::superres::DualTVL1OpticalFlow>();
}

Ptr<cv::superres::BroxOpticalFlow> cv::superres::createOptFlow_Brox_CUDA()
{
    CV_Error(cv::Error::StsNotImplemented, "The called functionality is disabled for current build or platform");
    return Ptr<cv::superres::BroxOpticalFlow>();
}

Ptr<cv::superres::PyrLKOpticalFlow> cv::superres::createOptFlow_PyrLK_CUDA()
{
    CV_Error(cv::Error::StsNotImplemented, "The called functionality is disabled for current build or platform");
    return Ptr<cv::superres::PyrLKOpticalFlow>();
}

#else // HAVE_OPENCV_CUDAOPTFLOW

namespace
{
    class GpuOpticalFlow : public virtual cv::superres::DenseOpticalFlowExt
    {
    public:
        explicit GpuOpticalFlow(int work_type);

        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

    protected:
        virtual void impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2) = 0;

    private:
        int work_type_;
        GpuMat buf_[6];
        GpuMat u_, v_, flow_;
    };

    GpuOpticalFlow::GpuOpticalFlow(int work_type) : work_type_(work_type)
    {
    }

    void GpuOpticalFlow::calc(InputArray _frame0, InputArray _frame1, OutputArray _flow1, OutputArray _flow2)
    {
        CV_INSTRUMENT_REGION()

        GpuMat frame0 = arrGetGpuMat(_frame0, buf_[0]);
        GpuMat frame1 = arrGetGpuMat(_frame1, buf_[1]);

        CV_Assert( frame1.type() == frame0.type() );
        CV_Assert( frame1.size() == frame0.size() );

        GpuMat input0 = convertToType(frame0, work_type_, buf_[2], buf_[3]);
        GpuMat input1 = convertToType(frame1, work_type_, buf_[4], buf_[5]);

        if (_flow2.needed() && _flow1.kind() == _InputArray::CUDA_GPU_MAT && _flow2.kind() == _InputArray::CUDA_GPU_MAT)
        {
            impl(input0, input1, _flow1.getGpuMatRef(), _flow2.getGpuMatRef());
            return;
        }

        impl(input0, input1, u_, v_);

        if (_flow2.needed())
        {
            arrCopy(u_, _flow1);
            arrCopy(v_, _flow2);
        }
        else
        {
            GpuMat src[] = {u_, v_};
            merge(src, 2, flow_);
            arrCopy(flow_, _flow1);
        }
    }

    void GpuOpticalFlow::collectGarbage()
    {
        for (int i = 0; i < 6; ++i)
            buf_[i].release();
        u_.release();
        v_.release();
        flow_.release();
    }
}

///////////////////////////////////////////////////////////////////
// Brox_CUDA

namespace
{
    class Brox_CUDA : public GpuOpticalFlow, public virtual cv::superres::BroxOpticalFlow
    {
    public:
        Brox_CUDA();
        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

        CV_IMPL_PROPERTY(double, Alpha, alpha_)
        CV_IMPL_PROPERTY(double, Gamma, gamma_)
        CV_IMPL_PROPERTY(double, ScaleFactor, scaleFactor_)
        CV_IMPL_PROPERTY(int, InnerIterations, innerIterations_)
        CV_IMPL_PROPERTY(int, OuterIterations, outerIterations_)
        CV_IMPL_PROPERTY(int, SolverIterations, solverIterations_)

    protected:
        void impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2);

    private:
        double alpha_;
        double gamma_;
        double scaleFactor_;
        int innerIterations_;
        int outerIterations_;
        int solverIterations_;

        Ptr<cuda::BroxOpticalFlow> alg_;
    };

    Brox_CUDA::Brox_CUDA() : GpuOpticalFlow(CV_32FC1)
    {
        alg_ = cuda::BroxOpticalFlow::create(0.197f, 50.0f, 0.8f, 10, 77, 10);

        alpha_ = alg_->getFlowSmoothness();
        gamma_ = alg_->getGradientConstancyImportance();
        scaleFactor_ = alg_->getPyramidScaleFactor();
        innerIterations_ = alg_->getInnerIterations();
        outerIterations_ = alg_->getOuterIterations();
        solverIterations_ = alg_->getSolverIterations();
    }

    void Brox_CUDA::calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2)
    {
        GpuOpticalFlow::calc(frame0, frame1, flow1, flow2);
    }

    void Brox_CUDA::impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2)
    {
        alg_->setFlowSmoothness(alpha_);
        alg_->setGradientConstancyImportance(gamma_);
        alg_->setPyramidScaleFactor(scaleFactor_);
        alg_->setInnerIterations(innerIterations_);
        alg_->setOuterIterations(outerIterations_);
        alg_->setSolverIterations(solverIterations_);

        GpuMat flow;
        alg_->calc(input0, input1, flow);

        GpuMat flows[2];
        cuda::split(flow, flows);

        dst1 = flows[0];
        dst2 = flows[1];
    }

    void Brox_CUDA::collectGarbage()
    {
        alg_ = cuda::BroxOpticalFlow::create(alpha_, gamma_, scaleFactor_, innerIterations_, outerIterations_, solverIterations_);
        GpuOpticalFlow::collectGarbage();
    }
}

Ptr<cv::superres::BroxOpticalFlow> cv::superres::createOptFlow_Brox_CUDA()
{
    return makePtr<Brox_CUDA>();
}

///////////////////////////////////////////////////////////////////
// PyrLK_CUDA

namespace
{
    class PyrLK_CUDA : public GpuOpticalFlow, public cv::superres::PyrLKOpticalFlow
    {
    public:
        PyrLK_CUDA();
        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

        CV_IMPL_PROPERTY(int, WindowSize, winSize_)
        CV_IMPL_PROPERTY(int, MaxLevel, maxLevel_)
        CV_IMPL_PROPERTY(int, Iterations, iterations_)

    protected:
        void impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2);

    private:
        int winSize_;
        int maxLevel_;
        int iterations_;

        Ptr<cuda::DensePyrLKOpticalFlow> alg_;
    };

    PyrLK_CUDA::PyrLK_CUDA() : GpuOpticalFlow(CV_8UC1)
    {
        alg_ = cuda::DensePyrLKOpticalFlow::create();

        winSize_ = alg_->getWinSize().width;
        maxLevel_ = alg_->getMaxLevel();
        iterations_ = alg_->getNumIters();
    }

    void PyrLK_CUDA::calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2)
    {
        GpuOpticalFlow::calc(frame0, frame1, flow1, flow2);
    }

    void PyrLK_CUDA::impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2)
    {
        alg_->setWinSize(Size(winSize_, winSize_));
        alg_->setMaxLevel(maxLevel_);
        alg_->setNumIters(iterations_);

        GpuMat flow;
        alg_->calc(input0, input1, flow);

        GpuMat flows[2];
        cuda::split(flow, flows);

        dst1 = flows[0];
        dst2 = flows[1];
    }

    void PyrLK_CUDA::collectGarbage()
    {
        alg_ = cuda::DensePyrLKOpticalFlow::create();
        GpuOpticalFlow::collectGarbage();
    }
}

Ptr<cv::superres::PyrLKOpticalFlow> cv::superres::createOptFlow_PyrLK_CUDA()
{
    return makePtr<PyrLK_CUDA>();
}

///////////////////////////////////////////////////////////////////
// Farneback_CUDA

namespace
{
    class Farneback_CUDA : public GpuOpticalFlow, public cv::superres::FarnebackOpticalFlow
    {
    public:
        Farneback_CUDA();
        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

        CV_IMPL_PROPERTY(double, PyrScale, pyrScale_)
        CV_IMPL_PROPERTY(int, LevelsNumber, numLevels_)
        CV_IMPL_PROPERTY(int, WindowSize, winSize_)
        CV_IMPL_PROPERTY(int, Iterations, numIters_)
        CV_IMPL_PROPERTY(int, PolyN, polyN_)
        CV_IMPL_PROPERTY(double, PolySigma, polySigma_)
        CV_IMPL_PROPERTY(int, Flags, flags_)

    protected:
        void impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2);

    private:
        double pyrScale_;
        int numLevels_;
        int winSize_;
        int numIters_;
        int polyN_;
        double polySigma_;
        int flags_;

        Ptr<cuda::FarnebackOpticalFlow> alg_;
    };

    Farneback_CUDA::Farneback_CUDA() : GpuOpticalFlow(CV_8UC1)
    {
        alg_ = cuda::FarnebackOpticalFlow::create();

        pyrScale_ = alg_->getPyrScale();
        numLevels_ = alg_->getNumLevels();
        winSize_ = alg_->getWinSize();
        numIters_ = alg_->getNumIters();
        polyN_ = alg_->getPolyN();
        polySigma_ = alg_->getPolySigma();
        flags_ = alg_->getFlags();
    }

    void Farneback_CUDA::calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2)
    {
        GpuOpticalFlow::calc(frame0, frame1, flow1, flow2);
    }

    void Farneback_CUDA::impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2)
    {
        alg_->setPyrScale(pyrScale_);
        alg_->setNumLevels(numLevels_);
        alg_->setWinSize(winSize_);
        alg_->setNumIters(numIters_);
        alg_->setPolyN(polyN_);
        alg_->setPolySigma(polySigma_);
        alg_->setFlags(flags_);

        GpuMat flow;
        alg_->calc(input0, input1, flow);

        GpuMat flows[2];
        cuda::split(flow, flows);

        dst1 = flows[0];
        dst2 = flows[1];
    }

    void Farneback_CUDA::collectGarbage()
    {
        alg_ = cuda::FarnebackOpticalFlow::create();
        GpuOpticalFlow::collectGarbage();
    }
}

Ptr<cv::superres::FarnebackOpticalFlow> cv::superres::createOptFlow_Farneback_CUDA()
{
    return makePtr<Farneback_CUDA>();
}

///////////////////////////////////////////////////////////////////
// DualTVL1_CUDA

namespace
{
    class DualTVL1_CUDA : public GpuOpticalFlow, public cv::superres::DualTVL1OpticalFlow
    {
    public:
        DualTVL1_CUDA();
        void calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2);
        void collectGarbage();

        CV_IMPL_PROPERTY(double, Tau, tau_)
        CV_IMPL_PROPERTY(double, Lambda, lambda_)
        CV_IMPL_PROPERTY(double, Theta, theta_)
        CV_IMPL_PROPERTY(int, ScalesNumber, nscales_)
        CV_IMPL_PROPERTY(int, WarpingsNumber, warps_)
        CV_IMPL_PROPERTY(double, Epsilon, epsilon_)
        CV_IMPL_PROPERTY(int, Iterations, iterations_)
        CV_IMPL_PROPERTY(bool, UseInitialFlow, useInitialFlow_)

    protected:
        void impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2);

    private:
        double tau_;
        double lambda_;
        double theta_;
        int nscales_;
        int warps_;
        double epsilon_;
        int iterations_;
        bool useInitialFlow_;

        Ptr<cuda::OpticalFlowDual_TVL1> alg_;
    };

    DualTVL1_CUDA::DualTVL1_CUDA() : GpuOpticalFlow(CV_8UC1)
    {
        alg_ = cuda::OpticalFlowDual_TVL1::create();

        tau_ = alg_->getTau();
        lambda_ = alg_->getLambda();
        theta_ = alg_->getTheta();
        nscales_ = alg_->getNumScales();
        warps_ = alg_->getNumWarps();
        epsilon_ = alg_->getEpsilon();
        iterations_ = alg_->getNumIterations();
        useInitialFlow_ = alg_->getUseInitialFlow();
    }

    void DualTVL1_CUDA::calc(InputArray frame0, InputArray frame1, OutputArray flow1, OutputArray flow2)
    {
        GpuOpticalFlow::calc(frame0, frame1, flow1, flow2);
    }

    void DualTVL1_CUDA::impl(const GpuMat& input0, const GpuMat& input1, GpuMat& dst1, GpuMat& dst2)
    {
        alg_->setTau(tau_);
        alg_->setLambda(lambda_);
        alg_->setTheta(theta_);
        alg_->setNumScales(nscales_);
        alg_->setNumWarps(warps_);
        alg_->setEpsilon(epsilon_);
        alg_->setNumIterations(iterations_);
        alg_->setUseInitialFlow(useInitialFlow_);

        GpuMat flow;
        alg_->calc(input0, input1, flow);

        GpuMat flows[2];
        cuda::split(flow, flows);

        dst1 = flows[0];
        dst2 = flows[1];
    }

    void DualTVL1_CUDA::collectGarbage()
    {
        alg_ = cuda::OpticalFlowDual_TVL1::create();
        GpuOpticalFlow::collectGarbage();
    }
}

Ptr<cv::superres::DualTVL1OpticalFlow> cv::superres::createOptFlow_DualTVL1_CUDA()
{
    return makePtr<DualTVL1_CUDA>();
}

#endif // HAVE_OPENCV_CUDAOPTFLOW
