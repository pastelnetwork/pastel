#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "support/allocators/zeroafterfree.h"
#include "serialize.h"

#include <algorithm>
#include <assert.h>
#include <ios>
#include <limits>
#include <map>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <utility>
#include <vector>

template<typename Stream>
class OverrideStream
{
    Stream* stream;

    const int nType;
    const int nVersion;

public:
    OverrideStream(Stream* stream_, int nType_, int nVersion_) : stream(stream_), nType(nType_), nVersion(nVersion_) {}

    template<typename T>
    OverrideStream<Stream>& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }

    template<typename T>
    OverrideStream<Stream>& operator>>(T&& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }

    void write(const char* pch, size_t nSize)
    {
        stream->write(pch, nSize);
    }

    void read(char* pch, size_t nSize)
    {
        stream->read(pch, nSize);
    }

    int GetVersion() const { return nVersion; }
    int GetType() const { return nType; }
};

template<typename S>
OverrideStream<S> WithVersion(S* s, int nVersion)
{
    return OverrideStream<S>(s, s->GetType(), nVersion);
}

/** Double ended buffer combining vector and stream-like interfaces.
 *
 * >> and << read and write unformatted data using the above serialization templates.
 * Fills with data in linear time; some stringstream implementations take N^2 time.
 */
template<typename SerializeType>
class CBaseDataStream
{
protected:
    typedef SerializeType vector_type;
    vector_type vch;
    size_t nReadPos;

    int m_nType;
    int m_nVersion;

public:

    typedef typename vector_type::allocator_type   allocator_type;
    typedef typename vector_type::size_type        size_type;
    typedef typename vector_type::difference_type  difference_type;
    typedef typename vector_type::reference        reference;
    typedef typename vector_type::const_reference  const_reference;
    typedef typename vector_type::value_type       value_type;
    typedef typename vector_type::iterator         iterator;
    typedef typename vector_type::const_iterator   const_iterator;
    typedef typename vector_type::reverse_iterator reverse_iterator;

    explicit CBaseDataStream(const int nType, const int nVersion)
    {
        Init(nType, nVersion);
    }

    CBaseDataStream(const_iterator pbegin, const_iterator pend, const int nType, const int nVersion) : 
        vch(pbegin, pend)
    {
        Init(nType, nVersion);
    }

#if !defined(_MSC_VER) || _MSC_VER >= 1300
    CBaseDataStream(const char* pbegin, const char* pend, const int nType, const int nVersion) : 
        vch(pbegin, pend)
    {
        Init(nType, nVersion);
    }
#endif

    CBaseDataStream(const vector_type& vchIn, const int nType, const int nVersion) : 
        vch(vchIn.cbegin(), vchIn.cend())
    {
        Init(nType, nVersion);
    }

    CBaseDataStream(const std::vector<char>& vchIn, const int nType, const int nVersion) : 
        vch(vchIn.cbegin(), vchIn.cend())
    {
        Init(nType, nVersion);
    }

    CBaseDataStream(const std::vector<unsigned char>& vchIn, const int nType, const int nVersion) : 
        vch(vchIn.cbegin(), vchIn.cend())
    {
        Init(nType, nVersion);
    }

    template <typename... Args>
    CBaseDataStream(const int nType, const int nVersion, Args&&... args)
    {
        Init(nType, nVersion);
        ::SerializeMany(*this, std::forward<Args>(args)...);
    }

    void Init(const int nType, const int nVersion)
    {
        nReadPos = 0;
        m_nType = nType;
        m_nVersion = nVersion;
    }

    CBaseDataStream& operator+=(const CBaseDataStream& b)
    {
        vch.insert(vch.end(), b.cbegin(), b.cend());
        return *this;
    }

    friend CBaseDataStream operator+(const CBaseDataStream& a, const CBaseDataStream& b)
    {
        CBaseDataStream ret = a;
        ret += b;
        return (ret);
    }

    std::string str() const
    {
        return (std::string(cbegin(), cend()));
    }


    //
    // Vector subset
    //
    const_iterator begin() const                     { return vch.begin() + nReadPos; }
    const_iterator cbegin() const                    { return vch.cbegin() + nReadPos; }
    iterator begin() { return vch.begin() + nReadPos; }
    const_iterator end() const                       { return vch.end(); }
    const_iterator cend() const                      { return vch.cend(); }
    iterator end() { return vch.end(); }
    size_type size() const                           { return vch.size() - nReadPos; }
    bool empty() const                               { return vch.size() == nReadPos; }
    void resize(size_type n, value_type c=0)         { vch.resize(n + nReadPos, c); }
    void reserve(size_type n)                        { vch.reserve(n + nReadPos); }
    const_reference operator[](size_type pos) const  { return vch[pos + nReadPos]; }
    reference operator[](size_type pos)              { return vch[pos + nReadPos]; }
    void clear()                                     { vch.clear(); nReadPos = 0; }
    iterator insert(iterator it, const char& x=char()) { return vch.insert(it, x); }
    void insert(iterator it, size_type n, const char& x) { vch.insert(it, n, x); }

    void insert(iterator it, std::vector<char>::const_iterator first, std::vector<char>::const_iterator last)
    {
        if (last == first) return;
        assert(last - first > 0);
        if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
        {
            // special case for inserting at the front when there's room
            nReadPos -= (last - first);
            memcpy(&vch[nReadPos], &first[0], last - first);
        }
        else
            vch.insert(it, first, last);
    }

#if !defined(_MSC_VER) || _MSC_VER >= 1300
    void insert(iterator it, const char* first, const char* last)
    {
        if (last == first) return;
        assert(last - first > 0);
        if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
        {
            // special case for inserting at the front when there's room
            nReadPos -= (last - first);
            memcpy(&vch[nReadPos], &first[0], last - first);
        }
        else
            vch.insert(it, first, last);
    }
#endif

    iterator erase(iterator it)
    {
        if (it == vch.begin() + nReadPos)
        {
            // special case for erasing from the front
            if (++nReadPos >= vch.size())
            {
                // whenever we reach the end, we take the opportunity to clear the buffer
                nReadPos = 0;
                return vch.erase(vch.begin(), vch.end());
            }
            return vch.begin() + nReadPos;
        }
        else
            return vch.erase(it);
    }

    iterator erase(iterator first, iterator last)
    {
        if (first == vch.begin() + nReadPos)
        {
            // special case for erasing from the front
            if (last == vch.end())
            {
                nReadPos = 0;
                return vch.erase(vch.begin(), vch.end());
            }
            else
            {
                nReadPos = (last - vch.begin());
                return last;
            }
        }
        else
            return vch.erase(first, last);
    }

    inline void Compact()
    {
        vch.erase(vch.begin(), vch.begin() + nReadPos);
        nReadPos = 0;
    }

    bool Rewind(size_type n)
    {
        // Rewind by n characters if the buffer hasn't been compacted yet
        if (n > nReadPos)
            return false;
        nReadPos -= n;
        return true;
    }

    //
    // Stream subset
    //
    bool eof() const noexcept     { return size() == 0; }
    CBaseDataStream* rdbuf()      { return this; }
    int in_avail() const noexcept { return size(); }

    void SetType(const int nType) noexcept    { m_nType = nType; }
    int GetType() const noexcept          { return m_nType; }
    void SetVersion(const int nVersion) noexcept { m_nVersion = nVersion; }
    int GetVersion() const noexcept       { return m_nVersion; }

    void read(char* pch, const size_t nSize)
    {
        if (nSize == 0) 
            return;

        if (!pch)
            throw std::ios_base::failure("CBaseDataStream::read(): cannot read from null pointer");

        // Read from the beginning of the buffer
        size_t nReadPosNext = nReadPos + nSize;
        if (nReadPosNext >= vch.size())
        {
            if (nReadPosNext > vch.size())
                throw std::ios_base::failure("CBaseDataStream::read(): end of data");
            memcpy(pch, &vch[nReadPos], nSize);
            nReadPos = 0;
            vch.clear();
            return;
        }
        memcpy(pch, &vch[nReadPos], nSize);
        nReadPos = nReadPosNext;
    }

    void ignore(const size_t nSize)
    {
        // Ignore from the beginning of the buffer
        const size_t nReadPosNext = nReadPos + nSize;
        if (nReadPosNext >= vch.size())
        {
            if (nReadPosNext > vch.size())
                throw std::ios_base::failure("CBaseDataStream::ignore(): end of data");
            nReadPos = 0;
            vch.clear();
            return;
        }
        nReadPos = nReadPosNext;
    }

    void write(const char* pch, const size_t nSize)
    {
        // Write to the end of the buffer
        vch.insert(vch.end(), pch, pch + nSize);
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        // Special case: stream << stream concatenates like stream += stream
        if (!vch.empty())
            s.write((char*)&vch[0], vch.size() * sizeof(vch[0]));
    }

    template<typename T>
    CBaseDataStream& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }

    template<typename T>
    CBaseDataStream& operator>>(T& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }

    void GetAndClear(CSerializeData &d) {
        d.insert(d.end(), begin(), end());
        clear();
    }
};

class CDataStream : public CBaseDataStream<CSerializeData>
{
public:
    explicit CDataStream(const int nType, const int nVersion) : 
        CBaseDataStream(nType, nVersion)
    {}

    CDataStream(const_iterator pbegin, const_iterator pend, const int nType, const int nVersion) :
        CBaseDataStream(pbegin, pend, nType, nVersion)
    {}

#if !defined(_MSC_VER) || _MSC_VER >= 1300
    CDataStream(const char* pbegin, const char* pend, const int nType, const int nVersion) :
        CBaseDataStream(pbegin, pend, nType, nVersion)
    {}
#endif

    CDataStream(const vector_type& vchIn, const int nType, const int nVersion) :
        CBaseDataStream(vchIn, nType, nVersion)
    {}

    CDataStream(const std::vector<char>& vchIn, const int nType, const int nVersion) :
        CBaseDataStream(vchIn, nType, nVersion)
    {}

    CDataStream(const std::vector<unsigned char>& vchIn, const int nType, const int nVersion) :
        CBaseDataStream(vchIn, nType, nVersion)
    {}

    template <typename... Args>
    CDataStream(const int nType, const int nVersion, Args&&... args) :
        CBaseDataStream(nType, nVersion, args...)
    {}

    template <typename T>
    CDataStream& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }

    template <typename T>
    CDataStream& operator>>(T& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    } };










/** Non-refcounted RAII wrapper for FILE*
 *
 * Will automatically close the file when it goes out of scope if not null.
 * If you're returning the file pointer, return file.release().
 * If you need to close the file early, use file.fclose() instead of fclose(file).
 */
class CAutoFile
{
private:
    // Disallow copies
    CAutoFile(const CAutoFile&);
    CAutoFile& operator=(const CAutoFile&);

    const int m_nType;
    const int m_nVersion;

    FILE* file;	

public:
    CAutoFile(FILE* filenew, const int nType, const int nVersion) : 
        m_nType(nType), m_nVersion(nVersion)
    {
        file = filenew;
    }

    ~CAutoFile()
    {
        fclose();
    }

    void fclose()
    {
        if (file) {
            ::fclose(file);
            file = nullptr;
        }
    }

    /** Get wrapped FILE* with transfer of ownership.
     * @note This will invalidate the CAutoFile object, and makes it the responsibility of the caller
     * of this function to clean up the returned FILE*.
     */
    FILE* release()             { FILE* ret = file; file = nullptr; return ret; }

    /** Get wrapped FILE* without transfer of ownership.
     * @note Ownership of the FILE* will remain with this class. Use this only if the scope of the
     * CAutoFile outlives use of the passed pointer.
     */
    FILE* Get() const           { return file; }

    /** Return true if the wrapped FILE* is NULL, false otherwise.
     */
    bool IsNull() const         { return (file == nullptr); }

    //
    // Stream subset
    //
    int GetType() const noexcept    { return m_nType; }
    int GetVersion() const noexcept { return m_nVersion; }

    void read(char* pch, size_t nSize)
    {
        if (!file)
            throw std::ios_base::failure("CAutoFile::read: file handle is NULL");
        if (fread(pch, 1, nSize, file) != nSize)
            throw std::ios_base::failure(feof(file) ? "CAutoFile::read: end of file" : "CAutoFile::read: fread failed");
    }

    void ignore(const size_t nSizeToSkip)
    {
        if (!file)
            throw std::ios_base::failure("CAutoFile::ignore: file handle is NULL");
        unsigned char data[4096];
        size_t nSize = nSizeToSkip;
        while (nSize > 0)
        {
            size_t nNow = std::min<size_t>(nSize, sizeof(data));
            if (fread(data, 1, nNow, file) != nNow)
                throw std::ios_base::failure(feof(file) ? "CAutoFile::ignore: end of file" : "CAutoFile::read: fread failed");
            nSize -= nNow;
        }
    }

    void write(const char* pch, size_t nSize)
    {
        if (!file)
            throw std::ios_base::failure("CAutoFile::write: file handle is NULL");
        if (fwrite(pch, 1, nSize, file) != nSize)
            throw std::ios_base::failure("CAutoFile::write: write failed");
    }

    template<typename T>
    CAutoFile& operator<<(const T& obj)
    {
        // Serialize to this stream
        if (!file)
            throw std::ios_base::failure("CAutoFile::operator<<: file handle is NULL");
        ::Serialize(*this, obj);
        return (*this);
    }

    template<typename T>
    CAutoFile& operator>>(T& obj)
    {
        // Unserialize from this stream
        if (!file)
            throw std::ios_base::failure("CAutoFile::operator>>: file handle is NULL");
        ::Unserialize(*this, obj);
        return (*this);
    }
};

/** Non-refcounted RAII wrapper around a FILE* that implements a ring buffer to
 *  deserialize from. It guarantees the ability to rewind a given number of bytes.
 *
 *  Will automatically close the file when it goes out of scope if not null.
 *  If you need to close the file early, use file.fclose() instead of fclose(file).
 */
class CBufferedFile
{
private:
    // Disallow copies
    CBufferedFile(const CBufferedFile&) = delete;
    CBufferedFile& operator=(const CBufferedFile&) = delete;

    const int m_nType;
    const int m_nVersion;

    FILE *src;            // source file
    uint64_t nSrcPos;     // how many bytes have been read from source
    uint64_t nReadPos;    // how many bytes have been read from this
    uint64_t nReadLimit;  // up to which position we're allowed to read
    uint64_t nRewind;     // how many bytes we guarantee to rewind
    std::vector<char> vchBuf; // the buffer

protected:
    // read data from the source to fill the buffer
    bool Fill()
    {
        const uint64_t nPos = nSrcPos % vchBuf.size();
        uint64_t readNow = vchBuf.size() - nPos;
        const uint64_t nAvailable = vchBuf.size() - (nSrcPos - nReadPos) - nRewind;
        if (nAvailable < readNow)
            readNow = nAvailable;
        if (readNow == 0)
            return false;
        const size_t read = fread((void*)&vchBuf[nPos], 1, readNow, src);
        if (read == 0)
            throw std::ios_base::failure(feof(src) ? "CBufferedFile::Fill: end of file" : "CBufferedFile::Fill: fread failed");
        nSrcPos += read;
        return true;
    }

public:
    CBufferedFile(FILE *fileIn, uint64_t nBufSize, uint64_t nRewindIn, const int nType, const int nVersion) :
        m_nType(nType), 
        m_nVersion(nVersion), 
        nSrcPos(0), 
        nReadPos(0), 
        nReadLimit((uint64_t)(-1)), 
        nRewind(nRewindIn), 
        vchBuf(nBufSize, 0)
    {
        src = fileIn;
    }

    ~CBufferedFile()
    {
        fclose();
    }

    int GetVersion() const noexcept { return m_nVersion; }
    int GetType() const noexcept { return m_nType; }

    void fclose()
    {
        if (src) {
            ::fclose(src);
            src = nullptr;
        }
    }

    // check whether we're at the end of the source file
    bool eof() const {
        return nReadPos == nSrcPos && feof(src);
    }

    // read a number of bytes
    void read(char *pch, const size_t nSizeToRead)
    {
        if (nSizeToRead == 0)
            return;
        size_t nSize = nSizeToRead;
        if (!pch)
            throw std::ios_base::failure("CBufferedFile::read(): cannot read from null pointer");
        if (nSize + nReadPos > nReadLimit)
            throw std::ios_base::failure("Read attempted past buffer limit");
        if (nSize + nRewind > vchBuf.size())
            throw std::ios_base::failure("Read larger than buffer size");
        while (nSize > 0)
        {
            if (nReadPos == nSrcPos)
                Fill();
            const uint64_t pos = nReadPos % vchBuf.size();
            size_t nNow = nSize;
            if (nNow + pos > vchBuf.size())
                nNow = vchBuf.size() - pos;
            if (nNow + nReadPos > nSrcPos)
                nNow = nSrcPos - nReadPos;
            memcpy(pch, &vchBuf[pos], nNow);
            nReadPos += nNow;
            pch += nNow;
            nSize -= nNow;
        }
    }
    void write(const char* pch, const size_t nSize) {} // stub

    // return the current reading position
    uint64_t GetPos() const noexcept { return nReadPos; }

    // rewind to a given reading position
    bool SetPos(const uint64_t nPos) noexcept
    {
        nReadPos = nPos;
        if (nReadPos + nRewind < nSrcPos)
        {
            nReadPos = nSrcPos - nRewind;
            return false;
        }
        if (nReadPos > nSrcPos)
        {
            nReadPos = nSrcPos;
            return false;
        }
        return true;
    }

    bool Seek(const uint64_t nPos)
    {
        long nLongPos = static_cast<long>(nPos);
        if (nPos != (uint64_t)nLongPos)
            return false;
        if (fseek(src, nLongPos, SEEK_SET))
            return false;
        nLongPos = ftell(src);
        nSrcPos = nLongPos;
        nReadPos = nLongPos;
        return true;
    }

    // prevent reading beyond a certain position
    // no argument removes the limit
    bool SetLimit(uint64_t nPos = (uint64_t)(-1)) {
        if (nPos < nReadPos)
            return false;
        nReadLimit = nPos;
        return true;
    }

    template<typename T>
    CBufferedFile& operator>>(T& obj) {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }

    // search for a given byte in the stream, and remain positioned on it
    void FindByte(char ch) {
        while (true) {
            if (nReadPos == nSrcPos)
                Fill();
            if (vchBuf[nReadPos % vchBuf.size()] == ch)
                break;
            nReadPos++;
        }
    }
};

