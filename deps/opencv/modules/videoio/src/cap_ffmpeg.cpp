/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
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
//   * The name of Intel Corporation may not be used to endorse or promote products
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

#include <string>

#if defined HAVE_FFMPEG && !defined WIN32
#include "cap_ffmpeg_impl.hpp"
#else
#include "cap_ffmpeg_api.hpp"
#endif

static CvCreateFileCapture_Plugin icvCreateFileCapture_FFMPEG_p = 0;
static CvReleaseCapture_Plugin icvReleaseCapture_FFMPEG_p = 0;
static CvGrabFrame_Plugin icvGrabFrame_FFMPEG_p = 0;
static CvRetrieveFrame_Plugin icvRetrieveFrame_FFMPEG_p = 0;
static CvSetCaptureProperty_Plugin icvSetCaptureProperty_FFMPEG_p = 0;
static CvGetCaptureProperty_Plugin icvGetCaptureProperty_FFMPEG_p = 0;
static CvCreateVideoWriter_Plugin icvCreateVideoWriter_FFMPEG_p = 0;
static CvReleaseVideoWriter_Plugin icvReleaseVideoWriter_FFMPEG_p = 0;
static CvWriteFrame_Plugin icvWriteFrame_FFMPEG_p = 0;

static cv::Mutex _icvInitFFMPEG_mutex;

#if defined WIN32 || defined _WIN32
static const HMODULE cv_GetCurrentModule()
{
    HMODULE h = 0;
#if _WIN32_WINNT >= 0x0501
    ::GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCTSTR>(cv_GetCurrentModule),
        &h);
#endif
    return h;
}
#endif

class icvInitFFMPEG
{
public:
    static void Init()
    {
        cv::AutoLock al(_icvInitFFMPEG_mutex);
        static icvInitFFMPEG init;
    }

private:
    #if defined WIN32 || defined _WIN32
    HMODULE icvFFOpenCV;

    ~icvInitFFMPEG()
    {
        if (icvFFOpenCV)
        {
            FreeLibrary(icvFFOpenCV);
            icvFFOpenCV = 0;
        }
    }
    #endif

    icvInitFFMPEG()
    {
    #if defined WIN32 || defined _WIN32
        const wchar_t* module_name_ = L"opencv_ffmpeg"
            CVAUX_STRW(CV_MAJOR_VERSION) CVAUX_STRW(CV_MINOR_VERSION) CVAUX_STRW(CV_SUBMINOR_VERSION)
        #if (defined _MSC_VER && defined _M_X64) || (defined __GNUC__ && defined __x86_64__)
            L"_64"
        #endif
            L".dll";
    # ifdef WINRT
        icvFFOpenCV = LoadPackagedLibrary( module_name_, 0 );
    # else
        const std::wstring module_name(module_name_);

        const wchar_t* ffmpeg_env_path = _wgetenv(L"OPENCV_FFMPEG_DLL_DIR");
        std::wstring module_path =
                ffmpeg_env_path
                ? ((std::wstring(ffmpeg_env_path) + L"\\") + module_name)
                : module_name;

        icvFFOpenCV = LoadLibraryW(module_path.c_str());
        if(!icvFFOpenCV && !ffmpeg_env_path)
        {
            HMODULE m = cv_GetCurrentModule();
            if (m)
            {
                wchar_t path[MAX_PATH];
                size_t sz = GetModuleFileNameW(m, path, sizeof(path));
                if (sz > 0 && ERROR_SUCCESS == GetLastError())
                {
                    wchar_t* s = wcsrchr(path, L'\\');
                    if (s)
                    {
                        s[0] = 0;
                        module_path = (std::wstring(path) + L"\\") + module_name;
                        icvFFOpenCV = LoadLibraryW(module_path.c_str());
                    }
                }
            }
        }
    # endif

        if( icvFFOpenCV )
        {
            icvCreateFileCapture_FFMPEG_p =
                (CvCreateFileCapture_Plugin)GetProcAddress(icvFFOpenCV, "cvCreateFileCapture_FFMPEG");
            icvReleaseCapture_FFMPEG_p =
                (CvReleaseCapture_Plugin)GetProcAddress(icvFFOpenCV, "cvReleaseCapture_FFMPEG");
            icvGrabFrame_FFMPEG_p =
                (CvGrabFrame_Plugin)GetProcAddress(icvFFOpenCV, "cvGrabFrame_FFMPEG");
            icvRetrieveFrame_FFMPEG_p =
                (CvRetrieveFrame_Plugin)GetProcAddress(icvFFOpenCV, "cvRetrieveFrame_FFMPEG");
            icvSetCaptureProperty_FFMPEG_p =
                (CvSetCaptureProperty_Plugin)GetProcAddress(icvFFOpenCV, "cvSetCaptureProperty_FFMPEG");
            icvGetCaptureProperty_FFMPEG_p =
                (CvGetCaptureProperty_Plugin)GetProcAddress(icvFFOpenCV, "cvGetCaptureProperty_FFMPEG");
            icvCreateVideoWriter_FFMPEG_p =
                (CvCreateVideoWriter_Plugin)GetProcAddress(icvFFOpenCV, "cvCreateVideoWriter_FFMPEG");
            icvReleaseVideoWriter_FFMPEG_p =
                (CvReleaseVideoWriter_Plugin)GetProcAddress(icvFFOpenCV, "cvReleaseVideoWriter_FFMPEG");
            icvWriteFrame_FFMPEG_p =
                (CvWriteFrame_Plugin)GetProcAddress(icvFFOpenCV, "cvWriteFrame_FFMPEG");

#if 0
            if( icvCreateFileCapture_FFMPEG_p != 0 &&
                icvReleaseCapture_FFMPEG_p != 0 &&
                icvGrabFrame_FFMPEG_p != 0 &&
                icvRetrieveFrame_FFMPEG_p != 0 &&
                icvSetCaptureProperty_FFMPEG_p != 0 &&
                icvGetCaptureProperty_FFMPEG_p != 0 &&
                icvCreateVideoWriter_FFMPEG_p != 0 &&
                icvReleaseVideoWriter_FFMPEG_p != 0 &&
                icvWriteFrame_FFMPEG_p != 0 )
            {
                printf("Successfully initialized ffmpeg plugin!\n");
            }
            else
            {
                printf("Failed to load FFMPEG plugin: module handle=%p\n", icvFFOpenCV);
            }
#endif
        }
    #elif defined HAVE_FFMPEG
        icvCreateFileCapture_FFMPEG_p = (CvCreateFileCapture_Plugin)cvCreateFileCapture_FFMPEG;
        icvReleaseCapture_FFMPEG_p = (CvReleaseCapture_Plugin)cvReleaseCapture_FFMPEG;
        icvGrabFrame_FFMPEG_p = (CvGrabFrame_Plugin)cvGrabFrame_FFMPEG;
        icvRetrieveFrame_FFMPEG_p = (CvRetrieveFrame_Plugin)cvRetrieveFrame_FFMPEG;
        icvSetCaptureProperty_FFMPEG_p = (CvSetCaptureProperty_Plugin)cvSetCaptureProperty_FFMPEG;
        icvGetCaptureProperty_FFMPEG_p = (CvGetCaptureProperty_Plugin)cvGetCaptureProperty_FFMPEG;
        icvCreateVideoWriter_FFMPEG_p = (CvCreateVideoWriter_Plugin)cvCreateVideoWriter_FFMPEG;
        icvReleaseVideoWriter_FFMPEG_p = (CvReleaseVideoWriter_Plugin)cvReleaseVideoWriter_FFMPEG;
        icvWriteFrame_FFMPEG_p = (CvWriteFrame_Plugin)cvWriteFrame_FFMPEG;
    #endif
    }
};


class CvCapture_FFMPEG_proxy :
    public CvCapture
{
public:
    CvCapture_FFMPEG_proxy() { ffmpegCapture = 0; }
    virtual ~CvCapture_FFMPEG_proxy() { close(); }

    virtual double getProperty(int propId) const
    {
        return ffmpegCapture ? icvGetCaptureProperty_FFMPEG_p(ffmpegCapture, propId) : 0;
    }
    virtual bool setProperty(int propId, double value)
    {
        return ffmpegCapture ? icvSetCaptureProperty_FFMPEG_p(ffmpegCapture, propId, value)!=0 : false;
    }
    virtual bool grabFrame()
    {
        return ffmpegCapture ? icvGrabFrame_FFMPEG_p(ffmpegCapture)!=0 : false;
    }
    virtual IplImage* retrieveFrame(int)
    {
        unsigned char* data = 0;
        int step=0, width=0, height=0, cn=0;

        if (!ffmpegCapture ||
           !icvRetrieveFrame_FFMPEG_p(ffmpegCapture, &data, &step, &width, &height, &cn))
            return 0;
        cvInitImageHeader(&frame, cvSize(width, height), 8, cn);
        cvSetData(&frame, data, step);
        return &frame;
    }
    virtual bool open( const char* filename )
    {
        icvInitFFMPEG::Init();
        close();

        if( !icvCreateFileCapture_FFMPEG_p )
            return false;
        ffmpegCapture = icvCreateFileCapture_FFMPEG_p( filename );
        return ffmpegCapture != 0;
    }
    virtual void close()
    {
        if( ffmpegCapture && icvReleaseCapture_FFMPEG_p )
            icvReleaseCapture_FFMPEG_p( &ffmpegCapture );
        assert( ffmpegCapture == 0 );
        ffmpegCapture = 0;
    }

protected:
    void* ffmpegCapture;
    IplImage frame;
};


CvCapture* cvCreateFileCapture_FFMPEG_proxy(const char * filename)
{
    CvCapture_FFMPEG_proxy* result = new CvCapture_FFMPEG_proxy;
    if( result->open( filename ))
        return result;
    delete result;
    return 0;
}

class CvVideoWriter_FFMPEG_proxy :
    public CvVideoWriter
{
public:
    CvVideoWriter_FFMPEG_proxy() { ffmpegWriter = 0; }
    virtual ~CvVideoWriter_FFMPEG_proxy() { close(); }

    virtual bool writeFrame( const IplImage* image )
    {
        if(!ffmpegWriter)
            return false;
        CV_Assert(image->depth == 8);

        return icvWriteFrame_FFMPEG_p(ffmpegWriter, (const uchar*)image->imageData,
             image->widthStep, image->width, image->height, image->nChannels, image->origin) !=0;
    }
    virtual bool open( const char* filename, int fourcc, double fps, CvSize frameSize, bool isColor )
    {
        icvInitFFMPEG::Init();
        close();
        if( !icvCreateVideoWriter_FFMPEG_p )
            return false;
        ffmpegWriter = icvCreateVideoWriter_FFMPEG_p( filename, fourcc, fps, frameSize.width, frameSize.height, isColor );
        return ffmpegWriter != 0;
    }

    virtual void close()
    {
        if( ffmpegWriter && icvReleaseVideoWriter_FFMPEG_p )
            icvReleaseVideoWriter_FFMPEG_p( &ffmpegWriter );
        assert( ffmpegWriter == 0 );
        ffmpegWriter = 0;
    }

protected:
    void* ffmpegWriter;
};


CvVideoWriter* cvCreateVideoWriter_FFMPEG_proxy( const char* filename, int fourcc,
                                          double fps, CvSize frameSize, int isColor )
{
    CvVideoWriter_FFMPEG_proxy* result = new CvVideoWriter_FFMPEG_proxy;

    if( result->open( filename, fourcc, fps, frameSize, isColor != 0 ))
        return result;
    delete result;
    return 0;
}
