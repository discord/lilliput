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
// Copyright (C) 2009-2011, Willow Garage Inc., all rights reserved.
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

#if defined WIN32 || defined WINCE
    #include <windows.h>
    #undef small
    #undef min
    #undef max
    #undef abs
#endif

#if defined __linux__ || defined __APPLE__
    #include <unistd.h>
    #include <stdio.h>
    #include <sys/types.h>
    #if defined ANDROID
        #include <sys/sysconf.h>
    #elif defined __APPLE__
        #include <sys/sysctl.h>
    #endif
#endif

#ifdef _OPENMP
    #define HAVE_OPENMP
#endif

#ifdef __APPLE__
    #define HAVE_GCD
#endif

#if defined _MSC_VER && _MSC_VER >= 1600
    #define HAVE_CONCURRENCY
#endif

/* IMPORTANT: always use the same order of defines
   1. HAVE_TBB         - 3rdparty library, should be explicitly enabled
   2. HAVE_CSTRIPES    - 3rdparty library, should be explicitly enabled
   3. HAVE_OPENMP      - integrated to compiler, should be explicitly enabled
   4. HAVE_GCD         - system wide, used automatically        (APPLE only)
   5. WINRT            - system wide, used automatically        (Windows RT only)
   6. HAVE_CONCURRENCY - part of runtime, used automatically    (Windows only - MSVS 10, MSVS 11)
   7. HAVE_PTHREADS_PF - pthreads if available
*/

#if defined HAVE_TBB
    #include "tbb/tbb.h"
    #include "tbb/task.h"
    #include "tbb/tbb_stddef.h"
    #if TBB_INTERFACE_VERSION >= 8000
        #include "tbb/task_arena.h"
    #endif
    #undef min
    #undef max
#elif defined HAVE_CSTRIPES
    #include "C=.h"
    #undef shared
#elif defined HAVE_OPENMP
    #include <omp.h>
#elif defined HAVE_GCD
    #include <dispatch/dispatch.h>
    #include <pthread.h>
#elif defined WINRT && _MSC_VER < 1900
    #include <ppltasks.h>
#elif defined HAVE_CONCURRENCY
    #include <ppl.h>
#endif


#if defined HAVE_TBB
#  define CV_PARALLEL_FRAMEWORK "tbb"
#elif defined HAVE_CSTRIPES
#  define CV_PARALLEL_FRAMEWORK "cstripes"
#elif defined HAVE_OPENMP
#  define CV_PARALLEL_FRAMEWORK "openmp"
#elif defined HAVE_GCD
#  define CV_PARALLEL_FRAMEWORK "gcd"
#elif defined WINRT
#  define CV_PARALLEL_FRAMEWORK "winrt-concurrency"
#elif defined HAVE_CONCURRENCY
#  define CV_PARALLEL_FRAMEWORK "ms-concurrency"
#elif defined HAVE_PTHREADS_PF
#  define CV_PARALLEL_FRAMEWORK "pthreads"
#endif

namespace cv
{
    ParallelLoopBody::~ParallelLoopBody() {}
#ifdef HAVE_PTHREADS_PF
    void parallel_for_pthreads(const cv::Range& range, const cv::ParallelLoopBody& body, double nstripes);
    size_t parallel_pthreads_get_threads_num();
    void parallel_pthreads_set_threads_num(int num);
#endif
}


namespace
{
#ifdef CV_PARALLEL_FRAMEWORK
#ifdef ENABLE_INSTRUMENTATION
    static void SyncNodes(cv::instr::InstrNode *pNode)
    {
        std::vector<cv::instr::NodeDataTls*> data;
        pNode->m_payload.m_tls.gather(data);

        uint64 ticksMax = 0;
        int    threads  = 0;
        for(size_t i = 0; i < data.size(); i++)
        {
            if(data[i] && data[i]->m_ticksTotal)
            {
                ticksMax = MAX(ticksMax, data[i]->m_ticksTotal);
                pNode->m_payload.m_ticksTotal -= data[i]->m_ticksTotal;
                data[i]->m_ticksTotal = 0;
                threads++;
            }
        }
        pNode->m_payload.m_ticksTotal += ticksMax;
        pNode->m_payload.m_threads = MAX(pNode->m_payload.m_threads, threads);

        for(size_t i = 0; i < pNode->m_childs.size(); i++)
            SyncNodes(pNode->m_childs[i]);
    }
#endif

    class ParallelLoopBodyWrapper : public cv::ParallelLoopBody
    {
    public:
        ParallelLoopBodyWrapper(const cv::ParallelLoopBody& _body, const cv::Range& _r, double _nstripes)
        {

            body = &_body;
            wholeRange = _r;
            double len = wholeRange.end - wholeRange.start;
            nstripes = cvRound(_nstripes <= 0 ? len : MIN(MAX(_nstripes, 1.), len));

#ifdef ENABLE_INSTRUMENTATION
            pThreadRoot = cv::instr::getInstrumentTLSStruct().pCurrentNode;
#endif
        }
#ifdef ENABLE_INSTRUMENTATION
        ~ParallelLoopBodyWrapper()
        {
            for(size_t i = 0; i < pThreadRoot->m_childs.size(); i++)
                SyncNodes(pThreadRoot->m_childs[i]);
        }
#endif
        void operator()(const cv::Range& sr) const
        {
#ifdef ENABLE_INSTRUMENTATION
            {
                cv::instr::InstrTLSStruct *pInstrTLS = &cv::instr::getInstrumentTLSStruct();
                pInstrTLS->pCurrentNode = pThreadRoot; // Initialize TLS node for thread
            }
#endif
            CV_INSTRUMENT_REGION()

            cv::Range r;
            r.start = (int)(wholeRange.start +
                            ((uint64)sr.start*(wholeRange.end - wholeRange.start) + nstripes/2)/nstripes);
            r.end = sr.end >= nstripes ? wholeRange.end : (int)(wholeRange.start +
                            ((uint64)sr.end*(wholeRange.end - wholeRange.start) + nstripes/2)/nstripes);
            (*body)(r);
        }
        cv::Range stripeRange() const { return cv::Range(0, nstripes); }

    protected:
        const cv::ParallelLoopBody* body;
        cv::Range wholeRange;
        int nstripes;
#ifdef ENABLE_INSTRUMENTATION
        cv::instr::InstrNode *pThreadRoot;
#endif
    };

#if defined HAVE_TBB
    class ProxyLoopBody : public ParallelLoopBodyWrapper
    {
    public:
        ProxyLoopBody(const cv::ParallelLoopBody& _body, const cv::Range& _r, double _nstripes)
        : ParallelLoopBodyWrapper(_body, _r, _nstripes)
        {}

        void operator ()(const tbb::blocked_range<int>& range) const
        {
            this->ParallelLoopBodyWrapper::operator()(cv::Range(range.begin(), range.end()));
        }
    };
#elif defined HAVE_CSTRIPES || defined HAVE_OPENMP
    typedef ParallelLoopBodyWrapper ProxyLoopBody;
#elif defined HAVE_GCD
    typedef ParallelLoopBodyWrapper ProxyLoopBody;
    static void block_function(void* context, size_t index)
    {
        ProxyLoopBody* ptr_body = static_cast<ProxyLoopBody*>(context);
        (*ptr_body)(cv::Range((int)index, (int)index + 1));
    }
#elif defined WINRT || defined HAVE_CONCURRENCY
    class ProxyLoopBody : public ParallelLoopBodyWrapper
    {
    public:
        ProxyLoopBody(const cv::ParallelLoopBody& _body, const cv::Range& _r, double _nstripes)
        : ParallelLoopBodyWrapper(_body, _r, _nstripes)
        {}

        void operator ()(int i) const
        {
            this->ParallelLoopBodyWrapper::operator()(cv::Range(i, i + 1));
        }
    };
#else
    typedef ParallelLoopBodyWrapper ProxyLoopBody;
#endif

static int numThreads = -1;

#if defined HAVE_TBB
static tbb::task_scheduler_init tbbScheduler(tbb::task_scheduler_init::deferred);
#elif defined HAVE_CSTRIPES
// nothing for C=
#elif defined HAVE_OPENMP
static int numThreadsMax = omp_get_max_threads();
#elif defined HAVE_GCD
// nothing for GCD
#elif defined WINRT
// nothing for WINRT
#elif defined HAVE_CONCURRENCY

class SchedPtr
{
    Concurrency::Scheduler* sched_;
public:
    Concurrency::Scheduler* operator->() { return sched_; }
    operator Concurrency::Scheduler*() { return sched_; }

    void operator=(Concurrency::Scheduler* sched)
    {
        if (sched_) sched_->Release();
        sched_ = sched;
    }

    SchedPtr() : sched_(0) {}
    ~SchedPtr() {}
};
static SchedPtr pplScheduler;

#endif

#endif // CV_PARALLEL_FRAMEWORK

} //namespace

/* ================================   parallel_for_  ================================ */

void cv::parallel_for_(const cv::Range& range, const cv::ParallelLoopBody& body, double nstripes)
{
    CV_INSTRUMENT_REGION_MT_FORK()
    if (range.empty())
        return;

#ifdef CV_PARALLEL_FRAMEWORK

    if(numThreads != 0)
    {
        ProxyLoopBody pbody(body, range, nstripes);
        cv::Range stripeRange = pbody.stripeRange();
        if( stripeRange.end - stripeRange.start == 1 )
        {
            body(range);
            return;
        }

#if defined HAVE_TBB

        tbb::parallel_for(tbb::blocked_range<int>(stripeRange.start, stripeRange.end), pbody);

#elif defined HAVE_CSTRIPES

        parallel(MAX(0, numThreads))
        {
            int offset = stripeRange.start;
            int len = stripeRange.end - offset;
            Range r(offset + CPX_RANGE_START(len), offset + CPX_RANGE_END(len));
            pbody(r);
            barrier();
        }

#elif defined HAVE_OPENMP

        #pragma omp parallel for schedule(dynamic)
        for (int i = stripeRange.start; i < stripeRange.end; ++i)
            pbody(Range(i, i + 1));

#elif defined HAVE_GCD

        dispatch_queue_t concurrent_queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
        dispatch_apply_f(stripeRange.end - stripeRange.start, concurrent_queue, &pbody, block_function);

#elif defined WINRT

        Concurrency::parallel_for(stripeRange.start, stripeRange.end, pbody);

#elif defined HAVE_CONCURRENCY

        if(!pplScheduler || pplScheduler->Id() == Concurrency::CurrentScheduler::Id())
        {
            Concurrency::parallel_for(stripeRange.start, stripeRange.end, pbody);
        }
        else
        {
            pplScheduler->Attach();
            Concurrency::parallel_for(stripeRange.start, stripeRange.end, pbody);
            Concurrency::CurrentScheduler::Detach();
        }

#elif defined HAVE_PTHREADS_PF

        parallel_for_pthreads(pbody.stripeRange(), pbody, pbody.stripeRange().size());

#else

#error You have hacked and compiling with unsupported parallel framework

#endif

    }
    else

#endif // CV_PARALLEL_FRAMEWORK
    {
        (void)nstripes;
        body(range);
    }
}

int cv::getNumThreads(void)
{
#ifdef CV_PARALLEL_FRAMEWORK

    if(numThreads == 0)
        return 1;

#endif

#if defined HAVE_TBB

    return tbbScheduler.is_active()
           ? numThreads
           : tbb::task_scheduler_init::default_num_threads();

#elif defined HAVE_CSTRIPES

    return numThreads > 0
            ? numThreads
            : cv::getNumberOfCPUs();

#elif defined HAVE_OPENMP

    return omp_get_max_threads();

#elif defined HAVE_GCD

    return 512; // the GCD thread pool limit

#elif defined WINRT

    return 0;

#elif defined HAVE_CONCURRENCY

    return 1 + (pplScheduler == 0
        ? Concurrency::CurrentScheduler::Get()->GetNumberOfVirtualProcessors()
        : pplScheduler->GetNumberOfVirtualProcessors());

#elif defined HAVE_PTHREADS_PF

        return parallel_pthreads_get_threads_num();

#else

    return 1;

#endif
}

void cv::setNumThreads( int threads )
{
    (void)threads;
#ifdef CV_PARALLEL_FRAMEWORK
    numThreads = threads;
#endif

#ifdef HAVE_TBB

    if(tbbScheduler.is_active()) tbbScheduler.terminate();
    if(threads > 0) tbbScheduler.initialize(threads);

#elif defined HAVE_CSTRIPES

    return; // nothing needed

#elif defined HAVE_OPENMP

    if(omp_in_parallel())
        return; // can't change number of openmp threads inside a parallel region

    omp_set_num_threads(threads > 0 ? threads : numThreadsMax);

#elif defined HAVE_GCD

    // unsupported
    // there is only private dispatch_queue_set_width() and only for desktop

#elif defined WINRT

    return;

#elif defined HAVE_CONCURRENCY

    if (threads <= 0)
    {
        pplScheduler = 0;
    }
    else if (threads == 1)
    {
        // Concurrency always uses >=2 threads, so we just disable it if 1 thread is requested
        numThreads = 0;
    }
    else if (pplScheduler == 0 || 1 + pplScheduler->GetNumberOfVirtualProcessors() != (unsigned int)threads)
    {
        pplScheduler = Concurrency::Scheduler::Create(Concurrency::SchedulerPolicy(2,
                       Concurrency::MinConcurrency, threads-1,
                       Concurrency::MaxConcurrency, threads-1));
    }

#elif defined HAVE_PTHREADS_PF

    parallel_pthreads_set_threads_num(threads);

#endif
}


int cv::getThreadNum(void)
{
#if defined HAVE_TBB
    #if TBB_INTERFACE_VERSION >= 9100
        return tbb::this_task_arena::current_thread_index();
    #elif TBB_INTERFACE_VERSION >= 8000
        return tbb::task_arena::current_thread_index();
    #else
        return 0;
    #endif
#elif defined HAVE_CSTRIPES
    return pix();
#elif defined HAVE_OPENMP
    return omp_get_thread_num();
#elif defined HAVE_GCD
    return (int)(size_t)(void*)pthread_self(); // no zero-based indexing
#elif defined WINRT
    return 0;
#elif defined HAVE_CONCURRENCY
    return std::max(0, (int)Concurrency::Context::VirtualProcessorId()); // zero for master thread, unique number for others but not necessary 1,2,3,...
#elif defined HAVE_PTHREADS_PF
    return (int)(size_t)(void*)pthread_self(); // no zero-based indexing
#else
    return 0;
#endif
}

#ifdef ANDROID
static inline int getNumberOfCPUsImpl()
{
   FILE* cpuPossible = fopen("/sys/devices/system/cpu/possible", "r");
   if(!cpuPossible)
       return 1;

   char buf[2000]; //big enough for 1000 CPUs in worst possible configuration
   char* pbuf = fgets(buf, sizeof(buf), cpuPossible);
   fclose(cpuPossible);
   if(!pbuf)
      return 1;

   //parse string of form "0-1,3,5-7,10,13-15"
   int cpusAvailable = 0;

   while(*pbuf)
   {
      const char* pos = pbuf;
      bool range = false;
      while(*pbuf && *pbuf != ',')
      {
          if(*pbuf == '-') range = true;
          ++pbuf;
      }
      if(*pbuf) *pbuf++ = 0;
      if(!range)
        ++cpusAvailable;
      else
      {
          int rstart = 0, rend = 0;
          sscanf(pos, "%d-%d", &rstart, &rend);
          cpusAvailable += rend - rstart + 1;
      }

   }
   return cpusAvailable ? cpusAvailable : 1;
}
#endif

int cv::getNumberOfCPUs(void)
{
#if defined WIN32 || defined _WIN32
    SYSTEM_INFO sysinfo;
#if (defined(_M_ARM) || defined(_M_X64) || defined(WINRT)) && _WIN32_WINNT >= 0x501
    GetNativeSystemInfo( &sysinfo );
#else
    GetSystemInfo( &sysinfo );
#endif

    return (int)sysinfo.dwNumberOfProcessors;
#elif defined ANDROID
    static int ncpus = getNumberOfCPUsImpl();
    return ncpus;
#elif defined __linux__
    return (int)sysconf( _SC_NPROCESSORS_ONLN );
#elif defined __APPLE__
    int numCPU=0;
    int mib[4];
    size_t len = sizeof(numCPU);

    /* set the mib for hw.ncpu */
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU;  // alternatively, try HW_NCPU;

    /* get the number of CPUs from the system */
    sysctl(mib, 2, &numCPU, &len, NULL, 0);

    if( numCPU < 1 )
    {
        mib[1] = HW_NCPU;
        sysctl( mib, 2, &numCPU, &len, NULL, 0 );

        if( numCPU < 1 )
            numCPU = 1;
    }

    return (int)numCPU;
#else
    return 1;
#endif
}

const char* cv::currentParallelFramework() {
#ifdef CV_PARALLEL_FRAMEWORK
    return CV_PARALLEL_FRAMEWORK;
#else
    return NULL;
#endif
}

CV_IMPL void cvSetNumThreads(int nt)
{
    cv::setNumThreads(nt);
}

CV_IMPL int cvGetNumThreads()
{
    return cv::getNumThreads();
}

CV_IMPL int cvGetThreadNum()
{
    return cv::getThreadNum();
}
