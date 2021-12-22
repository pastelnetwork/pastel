// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gmock/gmock.h>
#include <zstd.h>
#include <zstd_errors.h>

#include <datacompressor.h>
#include <mnode/ticket-processor.h>
#include <utilstrencodings.h>

using namespace testing;
using namespace std;

// tests with real zstd library
class TestCompressedDataStream : 
	public CCompressedDataStream,
	public Test
{
public:
    static constexpr auto TEST_DATA = "Test Data To Compress";
    // [251] -> [22], ~55% compression
    static constexpr auto TEST_DATA_NODISCARD = R"(Data are not compressed if its size is less than some predefined size.
Also, compressed data can be discarded if compressed data size is greater than the original data size or compressed data size 
is only few percent less than the original data size.)";
    // [150] -> [163], ~108% compression
    static constexpr auto TEST_HEXDATA_IMCOMPRESSIBLE = 
        "032a4fb5095f47ff981cf7aae5bf5f3aeab6a70256b80d307ef5daeced3f461d9686a6f724295be87b614d65b98ab9378da940ef16b5a2b665440743ebd4f9a6d5b3f32cc6a216e4804ff7c3afe4e369694b525d95cdc7746baa132d108407888b7e43d96d5e0fb03088e0221c0d3ce4535079388aff6b390fed21807e2710124ecd638d7ab897944539120a2689e4815067f095732a";
    // [110] -> [108], ~98.18%
    static constexpr auto TEST_HEXDATA_BAD_COMPRESSION_RATIO = 
        "2a865c96102756f9b945e6900fde8d91263e4efe717008d9c0664fb4ca1b176c6c08df1334c660510921c7e87e9584338a0464468c2f9d3e01fe6790fe5a7bdfc6ea789fe2f6eb34ad9a473f7c5fd9da98739eb88e2cd01010101010101010101010101010101010101010101010";

    TestCompressedDataStream() :
		CCompressedDataStream(SER_NETWORK, DATASTREAM_VERSION)
    {}

    // get estimated compressed data size based on input data size
    MOCK_METHOD(size_t, GetEstimatedCompressedSize, (const size_t nSrcDataSize), (const, override));
    // get decompressed data size
    MOCK_METHOD(bool, GetDecompressedSize, (string& error, size_t &nDecompressedSize, const void* pCompressedData, const size_t nCompressedDataSize), (const, override));
    // compress data
    MOCK_METHOD(bool, LibDataCompress, (string & error, size_t& nCompressedSize, void* dst, const size_t nDstSize, const void* pSrcData, const size_t nSrcDataSize), (override));
    // decompress data
    MOCK_METHOD(bool, LibDataDecompress, (string & error, size_t& nDecompressedSize, void* dst, const size_t nDstSize, const void* pCompressedData, const size_t nCompressedDataSize), (override));

    // increase compress discard threshold to 7%
    double GetCompressDiscardThreshold() const noexcept override { return 7.; }

    void SetUp() override
    {
        // by default, all IDataCompressor calls delegated to the parent real class CCompressedDataStream
        ON_CALL(*this, GetEstimatedCompressedSize).WillByDefault([this](const size_t nSrcDataSize)
            { return this->CCompressedDataStream::GetEstimatedCompressedSize(nSrcDataSize); });
        ON_CALL(*this, GetDecompressedSize).WillByDefault([this](string &error, size_t &nDecompressedSize, const void* pCompressedData, const size_t nCompressedDataSize)
        {
            return this->CCompressedDataStream::GetDecompressedSize(error, nDecompressedSize, pCompressedData, nCompressedDataSize);
        });
        ON_CALL(*this, LibDataCompress).WillByDefault([this](string & error, size_t & nCompressedSize, void* dst, const size_t nDstSize, const void* pSrcData, const size_t nSrcDataSize) 
        {
            return this->CCompressedDataStream::LibDataCompress(error, nCompressedSize, dst, nDstSize, pSrcData, nSrcDataSize);
        });
        ON_CALL(*this, LibDataDecompress).WillByDefault([this](string& error, size_t& nDecompressedSize, void* dst, const size_t nDstSize, const void* pCompressedData, const size_t nCompressedDataSize)
        {
            return this->CCompressedDataStream::LibDataDecompress(error, nDecompressedSize, dst, nDstSize, pCompressedData, nCompressedDataSize);
        });
    }

protected:
    string error;

    static void AppendTestCompressedData(vector_type &vData)
    {
        const size_t nTestDataSize = char_traits<char>::length(TEST_DATA);
        const size_t nEstimatedCompressSize = ZSTD_compressBound(nTestDataSize);
        string s(nEstimatedCompressSize, 0);
        const size_t nCompressedSize = ZSTD_compress(s.data(), s.size(), TEST_DATA, nTestDataSize, ZSTD_DEFAULT_COMPRESS_LEVEL);
        ASSERT_FALSE(ZSTD_isError(nCompressedSize));
        ASSERT_LE(nCompressedSize, nEstimatedCompressSize);
        s.resize(nCompressedSize);
        vData.reserve(vData.size() + s.size());
        vData.insert(vData.cend(), s.cbegin(), s.cend());
    }

    void AppendTestData(const char *szTestData)
    {
        vch.insert(vch.cend(), szTestData, szTestData + strlen(szTestData));
    }

    void CheckNotCompressed(const size_t &nOldSize)
    {
        EXPECT_FALSE(IsCompressed());
        EXPECT_TRUE(error.empty());
        vector_type vData = move(vch);
        EXPECT_TRUE(!vData.empty());
        EXPECT_EQ(vData.size(), nOldSize);
        if (!vData.empty())
            EXPECT_EQ(vData[0], 42);
    }

    void SkipCompressionTest(const char *szTestData)
    {
        const char ch = static_cast<char>(42 | 0x80);
        vch = {ch};
        AppendTestData(vector_to_string(ParseHex(szTestData)).c_str());
        const size_t nOldSize = vch.size();

        EXPECT_CALL(*this, GetEstimatedCompressedSize);
        EXPECT_CALL(*this, LibDataCompress);

        EXPECT_TRUE(CompressData(error, 1, [](vector_type::iterator start, vector_type::iterator end) {
            if (start != end)
                *start = 42;
        }));
        CheckNotCompressed(nOldSize);
    }
};

TEST_F(TestCompressedDataStream, ctr)
{
    EXPECT_FALSE(IsCompressed());
    EXPECT_EQ(GetCompressorVersion(), COMPRESSOR_VERSION);
}

TEST_F(TestCompressedDataStream, SetData_NoCompressorVersion)
{
    // 3 bytes uncompressed
    vector_type vData{1,2,3};
    // should throw ios_base::failure(CBaseDataStream::read(): end of data)
    EXPECT_THROW(SetData(error, true, 3, move(vData)), ios_base::failure);
}

TEST_F(TestCompressedDataStream, SetData_NoCompressorDataSize)
{
    // 3 bytes uncompressed
    // 0x01 - compressor version
    vector_type vData {10, 11, 12, 1};
    EXPECT_THROW(SetData(error, true, 3, move(vData)), ios_base::failure);
}

TEST_F(TestCompressedDataStream, SetData_InvalidCompressorDataSize)
{
    // 3 bytes uncompressed
    // 0x02 - compressor version
    // 253(-3), 0x80 (-128), 0x3E(63) - encoded compact size 16000 (compressor size)
    vector_type vData{10, 11, 12, 2, -3, -128, 63 };
    AppendTestCompressedData(vData);

    EXPECT_FALSE(SetData(error, true, 3, move(vData)));
    EXPECT_TRUE(!error.empty());
}

TEST_F(TestCompressedDataStream, SetData_InvalidStartPos)
{
    vector_type vData{1, 1, 0 };
    AppendTestCompressedData(vData);
    EXPECT_FALSE(SetData(error, true, vData.size() + 1, move(vData)));
    EXPECT_TRUE(!error.empty());
}

// test setting empty compressed data
TEST_F(TestCompressedDataStream, SetData_EmptyCompressedData)
{
    // 3 bytes uncompressed
    // 0x01 - compressor version
    // 0x00 - compact size - extra compressor data
    vector_type vData {10, 11, 12,   1, 0};
    EXPECT_TRUE(SetData(error, true, 3, move(vData)));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(vch.empty());
}

TEST_F(TestCompressedDataStream, SetData)
{
    // 2 bytes uncompressed
    // 0x01 - compressor version
    // 0x00 - compact size - extra compressor data
    vector_type vData{10, 11,   1, 0};
    AppendTestCompressedData(vData);

    EXPECT_CALL(*this, GetDecompressedSize);
    EXPECT_CALL(*this, LibDataDecompress);

    EXPECT_TRUE(SetData(error, true, 2, move(vData)));
    EXPECT_TRUE(error.empty());
    // after decompression vch contains pure data
    EXPECT_STREQ(vector_to_string(vch).c_str(), TEST_DATA);
}

// test setting compressed data with unknown version
// should correctly skip specified compressor data
TEST_F(TestCompressedDataStream, SetData_NewVersion)
{
    // 3 bytes uncompressed
    // 0x05 - compressor version (new not supported version v5)
    // 0x03 - compact size - extra compressor data
    vector_type vData{10, 11, 12,   5, 3,   33, 44, 55};
    AppendTestCompressedData(vData);

    EXPECT_CALL(*this, GetDecompressedSize);
    EXPECT_CALL(*this, LibDataDecompress);
    
    EXPECT_TRUE(SetData(error, true, 3, move(vData)));
    EXPECT_TRUE(error.empty());
    // after decompression vch contains pure data
    EXPECT_STREQ(vector_to_string(vch).c_str(), TEST_DATA);
}

// setting not compressed data
TEST_F(TestCompressedDataStream, SetData_NotCompressed)
{
    vector_type vData;
    const size_t nTestDataSize = char_traits<char>::length(TEST_DATA);
    vData.reserve(nTestDataSize + 1);
    vData.emplace_back(42);
    vData.insert(vData.cend(), TEST_DATA, TEST_DATA + nTestDataSize);

    EXPECT_TRUE(SetData(error, false, 1, move(vData)));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(!vch.empty());
    if (!vch.empty())
    {
        EXPECT_EQ(vch[0], 42);
        vch.erase(vch.cbegin());
        EXPECT_STREQ(vector_to_string(vch).c_str(), TEST_DATA);
    }
}

TEST_F(TestCompressedDataStream, SetData_InvalidCompressedData)
{
    // 1-byte uncompressed
    // 0x01 - compressor version
    // 0x00 - compact size - extra compressor data
    vector_type vData{42, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    EXPECT_CALL(*this, GetDecompressedSize);

    EXPECT_FALSE(SetData(error, true, 1, move(vData)));
    EXPECT_TRUE(!error.empty());
}

TEST_F(TestCompressedDataStream, SetData_InvalidDecompressedSize)
{
    vector_type vData{1, 0};
    AppendTestCompressedData(vData);

    // return invalid decompressed size
    EXPECT_CALL(*this, GetDecompressedSize).WillOnce(
        DoAll(
            SetArgReferee<1>(100),
            Return(true))
        );
    EXPECT_CALL(*this, LibDataDecompress);

    EXPECT_FALSE(SetData(error, true, 0, move(vData)));
    EXPECT_TRUE(!error.empty());
}

TEST_F(TestCompressedDataStream, SetData_DecompressFail)
{
    vector_type vData{1, 0};
    AppendTestCompressedData(vData);

    EXPECT_CALL(*this, GetDecompressedSize);
    EXPECT_CALL(*this, LibDataDecompress).WillOnce(
        DoAll(
            SetArgReferee<0>("decompress error"), 
            Return(false)));
    
    EXPECT_FALSE(SetData(error, true, 0, move(vData)));
    EXPECT_TRUE(!error.empty());
}

TEST_F(TestCompressedDataStream, CompressData)
{
    const char ch = static_cast<char>(42 | 0x80);
    vch = {ch};
    AppendTestData(TEST_DATA_NODISCARD);

    EXPECT_CALL(*this, GetEstimatedCompressedSize);
    EXPECT_CALL(*this, LibDataCompress);
    EXPECT_CALL(*this, GetDecompressedSize);
    EXPECT_CALL(*this, LibDataDecompress);

    EXPECT_TRUE(CompressData(error, 1, [](vector_type::iterator start, vector_type::iterator end) 
        {
        if (start != end)
            *start = 42;
        }));
    EXPECT_TRUE(IsCompressed());
    EXPECT_TRUE(error.empty());
    vector_type vData = move(vch);
    EXPECT_TRUE(!vData.empty());
    if (!vData.empty())
    {
        EXPECT_EQ(vData[0], ch);
        EXPECT_TRUE(SetData(error, true, 1, move(vData)));
        EXPECT_TRUE(error.empty());
        // after decompression vch contains pure data
        EXPECT_STREQ(vector_to_string(vch).c_str(), TEST_DATA_NODISCARD);
    }
}

TEST_F(TestCompressedDataStream, CompressData_InvalidKeepUncompressedSize)
{
    vch = {42};
    AppendTestData(TEST_DATA_NODISCARD);

    EXPECT_FALSE(CompressData(error, 1000, [](vector_type::iterator start, vector_type::iterator end) {}));
    EXPECT_TRUE(!error.empty());
}

TEST_F(TestCompressedDataStream, CompressData_Fail)
{
    vch = {42};
    AppendTestData(TEST_DATA_NODISCARD);

    EXPECT_CALL(*this, GetEstimatedCompressedSize);
    EXPECT_CALL(*this, LibDataCompress).WillOnce(
        DoAll(
            SetArgReferee<0>("compress error"), 
            Return(false)));
    EXPECT_FALSE(CompressData(error, 1, [](vector_type::iterator start, vector_type::iterator end) {}));
    EXPECT_TRUE(!error.empty());
}

TEST_F(TestCompressedDataStream, CompressData_SmallSize)
{
    const char ch = static_cast<char>(42 | 0x80);
    vch = {ch};
    AppendTestData(TEST_DATA);
    const size_t nOldSize = vch.size();

    EXPECT_TRUE(CompressData(error, 1, [](vector_type::iterator start, vector_type::iterator end)
    {
        if (start != end)
            *start = 42;
    }));
    CheckNotCompressed(nOldSize);
}

TEST_F(TestCompressedDataStream, CompressData_Imcompressible)
{
    SkipCompressionTest(TEST_HEXDATA_IMCOMPRESSIBLE);
}

TEST_F(TestCompressedDataStream, CompressData_BadCompression)
{
    SkipCompressionTest(TEST_HEXDATA_BAD_COMPRESSION_RATIO);
}
