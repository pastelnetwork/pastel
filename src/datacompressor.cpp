// Copyright (c) 2018-2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <zstd.h>
#include <zstd_errors.h>

#include <datacompressor.h>
#include <str_utils.h>

using namespace std;

constexpr auto ERRMSG_INVALID_STREAM_POS = "Invalid starting stream position %zu, stream size = %zu";

// get estimated compressed data size based on input data size
size_t CCompressedDataStream::GetEstimatedCompressedSize(const size_t nSrcDataSize) const
{
    return ZSTD_compressBound(nSrcDataSize);
}

/**
 * Get decompressed data size.
 * 
 * \param error - returns error message in case it was not possible to retrieve decompressed size from the compressed data
 * \param nDecompressedSize - return decompressed data size
 * \param pCompressedData - compressed data
 * \param nCompressedDataSize - compressed data size
 * \return true if successfully retrieved decompressed data size, otherwise error message returned
 */
bool CCompressedDataStream::GetDecompressedSize(string& error, size_t& nDecompressedSize, const void* pCompressedData, const size_t nCompressedDataSize) const
{
    // Determine decompressed size
    // it is always present when compression is completed using single-pass function ZSTD_compress
    nDecompressedSize = ZSTD_getFrameContentSize(pCompressedData, nCompressedDataSize);

    // check for any zstd compression errors
    if (nDecompressedSize == ZSTD_CONTENTSIZE_ERROR)
    {
        error = "failed to retrieve decompressed data size";
        return false;
    }
    if (nDecompressedSize == ZSTD_CONTENTSIZE_UNKNOWN)
    {
        error = "decompressed data size cannot be determined";
        return false;
    }
    return true;
}

// compress data
bool CCompressedDataStream::LibDataCompress(std::string& error, size_t &nCompressedSize, void* dst, const size_t nDstSize, const void* pSrcData, const size_t nSrcDataSize)
{
    // Compress data starting from nKeepUncompressedSize
    nCompressedSize = ZSTD_compress(dst, nDstSize, pSrcData, nSrcDataSize, ZSTD_DEFAULT_COMPRESS_LEVEL);

    // check for any zstd compression errors
    if (ZSTD_isError(nCompressedSize))
    {
        error = SAFE_SZ(ZSTD_getErrorName(nCompressedSize));
        return false;
    }
    return true;
}

// decompress data
bool CCompressedDataStream::LibDataDecompress(std::string& error, size_t& nDecompressedSize, void* dst, const size_t nDstSize, const void* pCompressedData, const size_t nCompressedDataSize)
{
    // Decompress source data
    nDecompressedSize = ZSTD_decompress(
        dst,                    // destination decompressed buffer
        nDstSize,               // decompressed size saved in a compressed buffer
        pCompressedData,        // input compressed buffer
        nCompressedDataSize     // input compressed buffer size
    );

    // check for any zstd compression errors
    if (ZSTD_isError(nDecompressedSize))
    {
        error = SAFE_SZ(ZSTD_getErrorName(nDecompressedSize));
        return false;
    }
    return true;
}

/**
 * Set stream data, supports data compressed with zstd.
 * 
 * \param error - returns error message in case of failure
 * \param bCompressed - if true, vData passed in compressed format, otherwise regular data
 * \param nStreamPos - skip uncompressed data in vData up to this position
 * \param vData - (uncompressed data upto nStreamPos) + (compressed data)
 * \return true if successfully set data
 */
bool CCompressedDataStream::SetData(string& error, const bool bCompressed, const size_t nStreamPos, vector_type&& vData)
{
    m_bCompressed = bCompressed;
    vch = move(vData);
    bool bRet = false;
    do
    {
        // skip uncompressed data
        if (nStreamPos > vch.size())
        {
            error = strprintf(ERRMSG_INVALID_STREAM_POS, nStreamPos, vch.size());
            break;
        }
        nReadPos = nStreamPos;
        if (bCompressed)
            bRet = Decompress(error);
        else
            bRet = true;
    } while (false);
    return bRet;
}

/**
 * Compress stream data.
 * Do not compress data if uncompressed data size is less than some predefined threshold size.
 * If we're compressing data and as a result we get a compressed data size which is greater than
 * uncompressed data or only few percent less - we discard compressed data and use original uncompressed
 * data. "handler" lambda is used to rollback any changes related to compression in the uncompressed data block.
 * 
 * \param error - returns error message in case of failure
 * \param nKeepUncompressedSize - keep nStreamPos chars as uncompressed
 * \param handler - lambda to rollback any changes related to compression in the uncompressed data block (nKeepUncompressedSize bytes)
 * \return true if stream data were successfully compressed or result of compression discarded
 */
bool CCompressedDataStream::CompressData(std::string& error, const size_t nKeepUncompressedSize, fnUncompressedDataHandler handler)
{
    m_nCompressorVersion = COMPRESSOR_VERSION;
    size_t nDataSize = vch.size();
    bool bRet = false;
    do
    {
        if (nKeepUncompressedSize > nDataSize)
        {
            error = strprintf(ERRMSG_INVALID_STREAM_POS, nKeepUncompressedSize, vch.size());
            break;
        }
        // data size to compress
        nDataSize -= nKeepUncompressedSize;
        // do not compress data if uncompressed data size is less than some predefined threshold size
        if (nDataSize <= UNCOMPRESSED_SIZE_DISCARD_THRESHOLD)
        {
            bRet = true;
            m_bCompressed = false; // data has been left uncompressed
            // call handler function to rollback any changes in uncompressed data that can specify compression
            handler(vch.begin(), vch.begin() + nKeepUncompressedSize);
            break;
        }

        // estimate compressed data size
        const size_t nEstimatedCompressedSize = GetEstimatedCompressedSize(nDataSize);

        vector_type vOut;
        constexpr size_t nCompressorBlockDataSize = 2; // for v1
        // reserve size for:
        //   1) uncompressed data
        //   2) compressor data (version + specific compressor data)
        //   3) estimated compressed data size
        vOut.resize(nKeepUncompressedSize + nCompressorBlockDataSize + nEstimatedCompressedSize);

        // copy uncompressed data from stream
        memcpy(vOut.data(), vch.data(), nKeepUncompressedSize);
        size_t nCurPos = nKeepUncompressedSize;
        // write compressor version and specific data size (0 for v1)
        vOut[nCurPos] = m_nCompressorVersion;
        vOut[nCurPos + 1] = 0; // compact size
        nCurPos += nCompressorBlockDataSize;

        size_t nCompressedSize = 0;
        // Compress data starting from nKeepUncompressedSize
        if (!LibDataCompress(error, nCompressedSize,
                            vOut.data() + nCurPos,              // destination buffer
                            nEstimatedCompressedSize,           // destination buffer size
                            vch.data() + nKeepUncompressedSize, // source compressed data
                            nDataSize))                         // source compressed data size
            break;
        // discard compression data if:
        //   - compressed data size is greater than original uncompressed data size
        //   - compressed data size is only a few percent less than original uncompressed data size
        if (nCompressedSize + nCompressorBlockDataSize >= nDataSize ||
            100 - (static_cast<double>(nCompressedSize + nCompressorBlockDataSize) / nDataSize) * 100 <= GetCompressDiscardThreshold())
        {
            bRet = true;
            m_bCompressed = false; // data has been left uncompressed
            // call handler function to rollback any changes in uncompressed data that can specify compression
            handler(vch.begin(), vch.begin() + nKeepUncompressedSize);
            break;
        }
        // we actually have compressed data - set flag
        m_bCompressed = true;

        // resize to the sum of:
        //   1) uncompressed data size
        //   2) compressor data size
        //   2) real compressed data size
        vOut.resize(nCurPos + nCompressedSize);
        // now replace original stream vector with the generated one
        vch = move(vOut);
        // reset stream read position
        nReadPos = 0;
        bRet = true;
    } while (false);
    return bRet;
}

/**
 * Decompress stream data.
 * data format:
 *   v1:
 *      [1] compressor version
 *      [2..] compressor compact data size
 *      [...] compressed data
 * 
 * \param error - returns an error message in case of failure
 * \return true if 
 */
bool CCompressedDataStream::Decompress(string &error)
{
    bool bRet = false;
    error.clear();
    // read compressor version
    ::Unserialize(*this, m_nCompressorVersion);
    // read compressor data size
    const uint64_t nCompressorDataSize = ReadCompactSize(*this);
    do
    {
        // v1 does not have any data, so just skip some stream data
        // this way it can be forward compatible with all future compressor versions
        if (nReadPos + nCompressorDataSize > vch.size())
        {
            error = strprintf("invalid compressor data size %zu", nCompressorDataSize);
            break;
        }
        nReadPos += nCompressorDataSize;
        // when we reach end of the vector CDataStream erases the buffer
        // make sure that vector is not empty here
        if (vch.empty())
        {
            bRet = true;
            break;
        }
        // stream position now points to the start of the compressed data
        const void* pCompressedData = &vch[nReadPos];
        const size_t nCompressedDataSize = vch.size() - nReadPos;
    
        // Get decompressed size
        size_t nSavedDecompressedSize = 0;
        if (!GetDecompressedSize(error, nSavedDecompressedSize, pCompressedData, nCompressedDataSize))
            break;

        vector_type vOut;
        vOut.resize(nSavedDecompressedSize);

        // Decompress stream data
        size_t nDecompressedSize = 0;
        if (!LibDataDecompress(error, nDecompressedSize,
                               vOut.data(),            // destination decompressed buffer
                               nSavedDecompressedSize, // decompressed size saved in a compressed buffer
                               pCompressedData,        // input compressed buffer
                               nCompressedDataSize))   // input compressed buffer size
            break;

        // decompressed data size should match the one we got from the stream before decompression
        if (nDecompressedSize != nSavedDecompressedSize)
        {
            error = strprintf("Uncompressed data size does not match [%zu] != [%zu]", nDecompressedSize, nSavedDecompressedSize);
            break;
        }
        // now replace original vector with decompressed
        vch = move(vOut);
        nReadPos = 0; // reset stream read position
        bRet = true;
    } while (false);
    return bRet;
}
