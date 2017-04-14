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
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Copyright (C) 2014, Itseez Inc, all rights reserved.
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

namespace cv {
namespace ml {

//////////////////////////////////////////////////////////////////////////////////////////
//                                  Random trees                                        //
//////////////////////////////////////////////////////////////////////////////////////////
RTreeParams::RTreeParams()
{
    calcVarImportance = false;
    nactiveVars = 0;
    termCrit = TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 50, 0.1);
}

RTreeParams::RTreeParams(bool _calcVarImportance,
                         int _nactiveVars,
                         TermCriteria _termCrit )
{
    calcVarImportance = _calcVarImportance;
    nactiveVars = _nactiveVars;
    termCrit = _termCrit;
}


class DTreesImplForRTrees : public DTreesImpl
{
public:
    DTreesImplForRTrees()
    {
        params.setMaxDepth(5);
        params.setMinSampleCount(10);
        params.setRegressionAccuracy(0.f);
        params.useSurrogates = false;
        params.setMaxCategories(10);
        params.setCVFolds(0);
        params.use1SERule = false;
        params.truncatePrunedTree = false;
        params.priors = Mat();
    }
    virtual ~DTreesImplForRTrees() {}

    void clear()
    {
        DTreesImpl::clear();
        oobError = 0.;
        rng = RNG((uint64)-1);
    }

    const vector<int>& getActiveVars()
    {
        int i, nvars = (int)allVars.size(), m = (int)activeVars.size();
        for( i = 0; i < nvars; i++ )
        {
            int i1 = rng.uniform(0, nvars);
            int i2 = rng.uniform(0, nvars);
            std::swap(allVars[i1], allVars[i2]);
        }
        for( i = 0; i < m; i++ )
            activeVars[i] = allVars[i];
        return activeVars;
    }

    void startTraining( const Ptr<TrainData>& trainData, int flags )
    {
        DTreesImpl::startTraining(trainData, flags);
        int nvars = w->data->getNVars();
        int i, m = rparams.nactiveVars > 0 ? rparams.nactiveVars : cvRound(std::sqrt((double)nvars));
        m = std::min(std::max(m, 1), nvars);
        allVars.resize(nvars);
        activeVars.resize(m);
        for( i = 0; i < nvars; i++ )
            allVars[i] = varIdx[i];
    }

    void endTraining()
    {
        DTreesImpl::endTraining();
        vector<int> a, b;
        std::swap(allVars, a);
        std::swap(activeVars, b);
    }

    bool train( const Ptr<TrainData>& trainData, int flags )
    {
        startTraining(trainData, flags);
        int treeidx, ntrees = (rparams.termCrit.type & TermCriteria::COUNT) != 0 ?
            rparams.termCrit.maxCount : 10000;
        int i, j, k, vi, vi_, n = (int)w->sidx.size();
        int nclasses = (int)classLabels.size();
        double eps = (rparams.termCrit.type & TermCriteria::EPS) != 0 &&
            rparams.termCrit.epsilon > 0 ? rparams.termCrit.epsilon : 0.;
        vector<int> sidx(n);
        vector<uchar> oobmask(n);
        vector<int> oobidx;
        vector<int> oobperm;
        vector<double> oobres(n, 0.);
        vector<int> oobcount(n, 0);
        vector<int> oobvotes(n*nclasses, 0);
        int nvars = w->data->getNVars();
        int nallvars = w->data->getNAllVars();
        const int* vidx = !varIdx.empty() ? &varIdx[0] : 0;
        vector<float> samplebuf(nallvars);
        Mat samples = w->data->getSamples();
        float* psamples = samples.ptr<float>();
        size_t sstep0 = samples.step1(), sstep1 = 1;
        Mat sample0, sample(nallvars, 1, CV_32F, &samplebuf[0]);
        int predictFlags = _isClassifier ? (PREDICT_MAX_VOTE + RAW_OUTPUT) : PREDICT_SUM;

        bool calcOOBError = eps > 0 || rparams.calcVarImportance;
        double max_response = 0.;

        if( w->data->getLayout() == COL_SAMPLE )
            std::swap(sstep0, sstep1);

        if( !_isClassifier )
        {
            for( i = 0; i < n; i++ )
            {
                double val = std::abs(w->ord_responses[w->sidx[i]]);
                max_response = std::max(max_response, val);
            }
        }

        if( rparams.calcVarImportance )
            varImportance.resize(nallvars, 0.f);

        for( treeidx = 0; treeidx < ntrees; treeidx++ )
        {
            for( i = 0; i < n; i++ )
                oobmask[i] = (uchar)1;

            for( i = 0; i < n; i++ )
            {
                j = rng.uniform(0, n);
                sidx[i] = w->sidx[j];
                oobmask[j] = (uchar)0;
            }
            int root = addTree( sidx );
            if( root < 0 )
                return false;

            if( calcOOBError )
            {
                oobidx.clear();
                for( i = 0; i < n; i++ )
                {
                    if( oobmask[i] )
                        oobidx.push_back(i);
                }
                int n_oob = (int)oobidx.size();
                // if there is no out-of-bag samples, we can not compute OOB error
                // nor update the variable importance vector; so we proceed to the next tree
                if( n_oob == 0 )
                    continue;
                double ncorrect_responses = 0.;

                oobError = 0.;
                for( i = 0; i < n_oob; i++ )
                {
                    j = oobidx[i];
                    sample = Mat( nallvars, 1, CV_32F, psamples + sstep0*w->sidx[j], sstep1*sizeof(psamples[0]) );

                    double val = predictTrees(Range(treeidx, treeidx+1), sample, predictFlags);
                    if( !_isClassifier )
                    {
                        oobres[j] += val;
                        oobcount[j]++;
                        double true_val = w->ord_responses[w->sidx[j]];
                        double a = oobres[j]/oobcount[j] - true_val;
                        oobError += a*a;
                        val = (val - true_val)/max_response;
                        ncorrect_responses += std::exp( -val*val );
                    }
                    else
                    {
                        int ival = cvRound(val);
                        //Voting scheme to combine OOB errors of each tree
                        int* votes = &oobvotes[j*nclasses];
                        votes[ival]++;
                        int best_class = 0;
                        for( k = 1; k < nclasses; k++ )
                            if( votes[best_class] < votes[k] )
                                best_class = k;
                        int diff = best_class != w->cat_responses[w->sidx[j]];
                        oobError += diff;
                        ncorrect_responses += diff == 0;
                    }
                }

                oobError /= n_oob;
                if( rparams.calcVarImportance && n_oob > 1 )
                {
                    Mat sample_clone;
                    oobperm.resize(n_oob);
                    for( i = 0; i < n_oob; i++ )
                        oobperm[i] = oobidx[i];
                    for (i = n_oob - 1; i > 0; --i)  //Randomly shuffle indices so we can permute features
                    {
                        int r_i = rng.uniform(0, n_oob);
                        std::swap(oobperm[i], oobperm[r_i]);
                    }

                    for( vi_ = 0; vi_ < nvars; vi_++ )
                    {
                        vi = vidx ? vidx[vi_] : vi_; //Ensure that only the user specified predictors are used for training
                        double ncorrect_responses_permuted = 0;

                        for( i = 0; i < n_oob; i++ )
                        {
                            j = oobidx[i];
                            int vj = oobperm[i];
                            sample0 = Mat( nallvars, 1, CV_32F, psamples + sstep0*w->sidx[j], sstep1*sizeof(psamples[0]) );
                            sample0.copyTo(sample_clone); //create a copy so we don't mess up the original data
                            sample_clone.at<float>(vi) = psamples[sstep0*w->sidx[vj] + sstep1*vi];

                            double val = predictTrees(Range(treeidx, treeidx+1), sample_clone, predictFlags);
                            if( !_isClassifier )
                            {
                                val = (val - w->ord_responses[w->sidx[j]])/max_response;
                                ncorrect_responses_permuted += exp( -val*val );
                            }
                            else
                            {
                                ncorrect_responses_permuted += cvRound(val) == w->cat_responses[w->sidx[j]];
                            }
                        }
                        varImportance[vi] += (float)(ncorrect_responses - ncorrect_responses_permuted);
                    }
                }
            }
            if( calcOOBError && oobError < eps )
                break;
        }

        if( rparams.calcVarImportance )
        {
            for( vi_ = 0; vi_ < nallvars; vi_++ )
                varImportance[vi_] = std::max(varImportance[vi_], 0.f);
            normalize(varImportance, varImportance, 1., 0, NORM_L1);
        }
        endTraining();
        return true;
    }

    void writeTrainingParams( FileStorage& fs ) const
    {
        DTreesImpl::writeTrainingParams(fs);
        fs << "nactive_vars" << rparams.nactiveVars;
    }

    void write( FileStorage& fs ) const
    {
        if( roots.empty() )
            CV_Error( CV_StsBadArg, "RTrees have not been trained" );

        writeFormat(fs);
        writeParams(fs);

        fs << "oob_error" << oobError;
        if( !varImportance.empty() )
            fs << "var_importance" << varImportance;

        int k, ntrees = (int)roots.size();

        fs << "ntrees" << ntrees
           << "trees" << "[";

        for( k = 0; k < ntrees; k++ )
        {
            fs << "{";
            writeTree(fs, roots[k]);
            fs << "}";
        }

        fs << "]";
    }

    void readParams( const FileNode& fn )
    {
        DTreesImpl::readParams(fn);

        FileNode tparams_node = fn["training_params"];
        rparams.nactiveVars = (int)tparams_node["nactive_vars"];
    }

    void read( const FileNode& fn )
    {
        clear();

        //int nclasses = (int)fn["nclasses"];
        //int nsamples = (int)fn["nsamples"];
        oobError = (double)fn["oob_error"];
        int ntrees = (int)fn["ntrees"];

        readVectorOrMat(fn["var_importance"], varImportance);

        readParams(fn);

        FileNode trees_node = fn["trees"];
        FileNodeIterator it = trees_node.begin();
        CV_Assert( ntrees == (int)trees_node.size() );

        for( int treeidx = 0; treeidx < ntrees; treeidx++, ++it )
        {
            FileNode nfn = (*it)["nodes"];
            readTree(nfn);
        }
    }

    RTreeParams rparams;
    double oobError;
    vector<float> varImportance;
    vector<int> allVars, activeVars;
    RNG rng;
};


class RTreesImpl : public RTrees
{
public:
    CV_IMPL_PROPERTY(bool, CalculateVarImportance, impl.rparams.calcVarImportance)
    CV_IMPL_PROPERTY(int, ActiveVarCount, impl.rparams.nactiveVars)
    CV_IMPL_PROPERTY_S(TermCriteria, TermCriteria, impl.rparams.termCrit)

    CV_WRAP_SAME_PROPERTY(int, MaxCategories, impl.params)
    CV_WRAP_SAME_PROPERTY(int, MaxDepth, impl.params)
    CV_WRAP_SAME_PROPERTY(int, MinSampleCount, impl.params)
    CV_WRAP_SAME_PROPERTY(int, CVFolds, impl.params)
    CV_WRAP_SAME_PROPERTY(bool, UseSurrogates, impl.params)
    CV_WRAP_SAME_PROPERTY(bool, Use1SERule, impl.params)
    CV_WRAP_SAME_PROPERTY(bool, TruncatePrunedTree, impl.params)
    CV_WRAP_SAME_PROPERTY(float, RegressionAccuracy, impl.params)
    CV_WRAP_SAME_PROPERTY_S(cv::Mat, Priors, impl.params)

    RTreesImpl() {}
    virtual ~RTreesImpl() {}

    String getDefaultName() const { return "opencv_ml_rtrees"; }

    bool train( const Ptr<TrainData>& trainData, int flags )
    {
        if (impl.getCVFolds() != 0)
            CV_Error(Error::StsBadArg, "Cross validation for RTrees is not implemented");
        return impl.train(trainData, flags);
    }

    float predict( InputArray samples, OutputArray results, int flags ) const
    {
        return impl.predict(samples, results, flags);
    }

    void write( FileStorage& fs ) const
    {
        impl.write(fs);
    }

    void read( const FileNode& fn )
    {
        impl.read(fn);
    }

    Mat getVarImportance() const { return Mat_<float>(impl.varImportance, true); }
    int getVarCount() const { return impl.getVarCount(); }

    bool isTrained() const { return impl.isTrained(); }
    bool isClassifier() const { return impl.isClassifier(); }

    const vector<int>& getRoots() const { return impl.getRoots(); }
    const vector<Node>& getNodes() const { return impl.getNodes(); }
    const vector<Split>& getSplits() const { return impl.getSplits(); }
    const vector<int>& getSubsets() const { return impl.getSubsets(); }

    DTreesImplForRTrees impl;
};


Ptr<RTrees> RTrees::create()
{
    return makePtr<RTreesImpl>();
}

}}

// End of file.
