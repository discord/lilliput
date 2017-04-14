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

#ifndef _BITSTRM_H_
#define _BITSTRM_H_

#include <stdio.h>

namespace cv
{

enum
{
    RBS_THROW_EOS=-123,  // <end of stream> exception code
    RBS_THROW_FORB=-124,  // <forrbidden huffman code> exception code
    RBS_HUFF_FORB=2047,  // forrbidden huffman code "value"
    RBS_BAD_HEADER=-125 // invalid header
};

typedef unsigned long ulong;

// class RBaseStream - base class for other reading streams.
class RBaseStream
{
public:
    //methods
    RBaseStream();
    virtual ~RBaseStream();

    virtual bool  open( const String& filename );
    virtual bool  open( const Mat& buf );
    virtual void  close();
    bool          isOpened();
    void          setPos( int pos );
    int           getPos();
    void          skip( int bytes );

protected:

    bool    m_allocated;
    uchar*  m_start;
    uchar*  m_end;
    uchar*  m_current;
    FILE*   m_file;
    int     m_block_size;
    int     m_block_pos;
    bool    m_is_opened;

    virtual void  readBlock();
    virtual void  release();
    virtual void  allocate();
};


// class RLByteStream - uchar-oriented stream.
// l in prefix means that the least significant uchar of a multi-uchar value goes first
class RLByteStream : public RBaseStream
{
public:
    virtual ~RLByteStream();

    int     getByte();
    int     getBytes( void* buffer, int count );
    int     getWord();
    int     getDWord();
};

// class RMBitStream - uchar-oriented stream.
// m in prefix means that the most significant uchar of a multi-uchar value go first
class RMByteStream : public RLByteStream
{
public:
    virtual ~RMByteStream();

    int     getWord();
    int     getDWord();
};

// WBaseStream - base class for output streams
class WBaseStream
{
public:
    //methods
    WBaseStream();
    virtual ~WBaseStream();

    virtual bool  open( const String& filename );
    virtual bool  open( std::vector<uchar>& buf );
    virtual void  close();
    bool          isOpened();
    int           getPos();

protected:

    uchar*  m_start;
    uchar*  m_end;
    uchar*  m_current;
    int     m_block_size;
    int     m_block_pos;
    FILE*   m_file;
    bool    m_is_opened;
    std::vector<uchar>* m_buf;

    virtual void  writeBlock();
    virtual void  release();
    virtual void  allocate();
};


// class WLByteStream - uchar-oriented stream.
// l in prefix means that the least significant uchar of a multi-byte value goes first
class WLByteStream : public WBaseStream
{
public:
    virtual ~WLByteStream();

    void  putByte( int val );
    void  putBytes( const void* buffer, int count );
    void  putWord( int val );
    void  putDWord( int val );
};


// class WLByteStream - uchar-oriented stream.
// m in prefix means that the least significant uchar of a multi-byte value goes last
class WMByteStream : public WLByteStream
{
public:
    virtual ~WMByteStream();
    void  putWord( int val );
    void  putDWord( int val );
};

inline unsigned BSWAP(unsigned v)
{
    return (v<<24)|((v&0xff00)<<8)|((v>>8)&0xff00)|((unsigned)v>>24);
}

bool bsIsBigEndian( void );

}

#endif/*_BITSTRM_H_*/
