#pragma once
// Copyright (c) 2018-2023 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <stdexcept>

#include <utils/streams.h>

// default zstd compression level
constexpr int ZSTD_DEFAULT_COMPRESS_LEVEL = 22;

// data compressor interface
class IDataCompressor
{
public:
    virtual ~IDataCompressor() noexcept = default;

    // get estimated compressed data size based on input data size
    virtual size_t GetEstimatedCompressedSize(const size_t nSrcDataSize) const = 0;
    // get decompressed data size
    virtual bool GetDecompressedSize(std::string& error, size_t& nDecompressedSize, const void* pCompressedData, const size_t nCompressedDataSize) const = 0;
    // compress data
    virtual bool LibDataCompress(std::string& error, size_t& nCompressedSize, 
        void* dst, const size_t nDstSize, const void* pSrcData, const size_t nSrcDataSize) = 0;
    // decompress data
    virtual bool LibDataDecompress(std::string& error, size_t& nDecompressedSize,
        void* dst, const size_t nDstSize, const void* pCompressedData, const size_t nCompressedDataSize) = 0;
};

/**
 * CCompressedDataStream is used to serialize/deserialize compressed streams.
 * 
 * stream format:
 * v1:
 *   1) [1 byte] compressor version, uint8_t
 *   2) [1 byte] size of compressor specific data (0 for v1)
 *   3) [......] compressor specific data (size depends on field #2, no data for v1)
 *   4) [......] serialized compressed data (vector)
 */
class CCompressedDataStream : 
    public CDataStream,
    public IDataCompressor
{
public:
    // current compressor version
    static constexpr uint8_t COMPRESSOR_VERSION = 1;
    // if we're compressing data and as a result we get a compressed data size which is greater than
    // uncompressed data or only COMPRESS_DISCARD_THRESHOLD percent less - we discard compressed data and
    // just keep uncompressed
    static constexpr double COMPRESS_DISCARD_THRESHOLD = 3.;
    // skip compression if data size is less than the given threshold size
    static constexpr uint64_t UNCOMPRESSED_SIZE_DISCARD_THRESHOLD = 100;

    using fnUncompressedDataHandler = std::function<void(vector_type::iterator, vector_type::iterator)>;

    explicit CCompressedDataStream(const int nType, const int nVersion) :
        CDataStream(nType, nVersion),
        m_bCompressed(false),
        m_nCompressorVersion(COMPRESSOR_VERSION),
        m_nSavedCompressedSize(0),
        m_nSavedDecompressedSize(0)
    {}

    bool IsCompressed() const noexcept { return m_bCompressed; }
    uint8_t GetCompressorVersion() const noexcept { return m_nVersion; }
    size_t GetSavedCompressedSize() const noexcept { return m_nSavedCompressedSize; }
    size_t GetSavedDecompressedSize() const noexcept { return m_nSavedDecompressedSize; }

    // set stream data, supports data compressed with zstd
    bool SetData(std::string &error, const bool bCompressed, const size_t nStreamPos, vector_type&& vData,
        const bool bUncompressData = true);
    // compress stream data
    bool CompressData(std::string& error, const size_t nKeepUncompressedSize, fnUncompressedDataHandler handler);
    virtual double GetCompressDiscardThreshold() const noexcept { return COMPRESS_DISCARD_THRESHOLD; }

protected:
    // decompress stream data
    bool Decompress(std::string &error);

    // IDataCompressor interface
    // get estimated compressed data size based on input data size
    size_t GetEstimatedCompressedSize(const size_t nSrcDataSize) const override;
    // get decompressed data size
    bool GetDecompressedSize(std::string &error, size_t& nDecompressedSize, const void* pCompressedData, const size_t nCompressedDataSize) const override;
    // compress data
    bool LibDataCompress(std::string& error, size_t &nCompressedSize, 
        void* dst, const size_t nDstSize, const void* pSrcData, const size_t nSrcDataSize) override;
    // decompress data
    bool LibDataDecompress(std::string& error, size_t &nDecompressedSize, 
        void* dst, const size_t nDstSize, const void* pCompressedData, const size_t nCompressedDataSize) override;

    bool m_bCompressed;                 // if true - stream is compressed
    uint8_t m_nCompressorVersion;       // compressor version
    size_t m_nSavedCompressedSize;      // saved compressed data size
    size_t m_nSavedDecompressedSize;    // saved decompressed data size
};
