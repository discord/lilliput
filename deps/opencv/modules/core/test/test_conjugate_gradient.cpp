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
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
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
// In no event shall the OpenCV Foundation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/
#include "test_precomp.hpp"
#include <cstdlib>

static void mytest(cv::Ptr<cv::ConjGradSolver> solver,cv::Ptr<cv::MinProblemSolver::Function> ptr_F,cv::Mat& x,
        cv::Mat& etalon_x,double etalon_res){
    solver->setFunction(ptr_F);
    //int ndim=MAX(step.cols,step.rows);
    double res=solver->minimize(x);
    std::cout<<"res:\n\t"<<res<<std::endl;
    std::cout<<"x:\n\t"<<x<<std::endl;
    std::cout<<"etalon_res:\n\t"<<etalon_res<<std::endl;
    std::cout<<"etalon_x:\n\t"<<etalon_x<<std::endl;
    double tol = 1e-2;
    ASSERT_TRUE(std::abs(res-etalon_res)<tol);
    /*for(cv::Mat_<double>::iterator it1=x.begin<double>(),it2=etalon_x.begin<double>();it1!=x.end<double>();it1++,it2++){
        ASSERT_TRUE(std::abs((*it1)-(*it2))<tol);
    }*/
    std::cout<<"--------------------------\n";
}

class SphereF_CG:public cv::MinProblemSolver::Function{
public:
    int getDims() const { return 4; }
    double calc(const double* x)const{
        return x[0]*x[0]+x[1]*x[1]+x[2]*x[2]+x[3]*x[3];
    }
    // use automatically computed gradient
    /*void getGradient(const double* x,double* grad){
        for(int i=0;i<4;i++){
            grad[i]=2*x[i];
        }
    }*/
};
class RosenbrockF_CG:public cv::MinProblemSolver::Function{
    int getDims() const { return 2; }
    double calc(const double* x)const{
        return 100*(x[1]-x[0]*x[0])*(x[1]-x[0]*x[0])+(1-x[0])*(1-x[0]);
    }
    void getGradient(const double* x,double* grad){
            grad[0]=-2*(1-x[0])-400*(x[1]-x[0]*x[0])*x[0];
            grad[1]=200*(x[1]-x[0]*x[0]);
    }
};

TEST(Core_ConjGradSolver, regression_basic){
    cv::Ptr<cv::ConjGradSolver> solver=cv::ConjGradSolver::create();
#if 1
    {
        cv::Ptr<cv::MinProblemSolver::Function> ptr_F(new SphereF_CG());
        cv::Mat x=(cv::Mat_<double>(4,1)<<50.0,10.0,1.0,-10.0),
            etalon_x=(cv::Mat_<double>(1,4)<<0.0,0.0,0.0,0.0);
        double etalon_res=0.0;
        mytest(solver,ptr_F,x,etalon_x,etalon_res);
    }
#endif
#if 1
    {
        cv::Ptr<cv::MinProblemSolver::Function> ptr_F(new RosenbrockF_CG());
        cv::Mat x=(cv::Mat_<double>(2,1)<<0.0,0.0),
            etalon_x=(cv::Mat_<double>(2,1)<<1.0,1.0);
        double etalon_res=0.0;
        mytest(solver,ptr_F,x,etalon_x,etalon_res);
    }
#endif
}
