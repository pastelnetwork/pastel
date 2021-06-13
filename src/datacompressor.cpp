// Copyright (c) 2018-2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "datacompressor.h"
#include <string>
#include "zstd.h"

bool CDataCompressor::Compress(std::vector<unsigned char> &out) const
{
    // Determine bound of compress size
    const size_t est_compress_size = ZSTD_compressBound(data.size());
    out.resize(est_compress_size);

    // Compress
    size_t compress_size = ZSTD_compress(
        (void*)out.data(),
        est_compress_size,
        (void*)&data[0],
        data.size(),
        kDefaultZSTDCompressLevel
    );

    // TODO : get error message
    if (ZSTD_isError(compress_size)) {
        return false;
    }

    out.resize(compress_size);
    out.shrink_to_fit();

    return true;
}

bool CDataCompressor::Decompress(const std::vector<unsigned char> &in)
{
    // Determine bound of decmpress size
    const size_t est_decomp_size = ZSTD_getDecompressedSize((void*)in.data(), in.size());
    data.resize(est_decomp_size);

    // Decompress
    size_t const decomp_size = ZSTD_decompress(
        (void*)&data[0],
        est_decomp_size,
        (void*) in.data(),
        data.size()
    );

    // TODO : get error message
    if (ZSTD_isError(decomp_size)) {
        return false;
    }

    data.resize(decomp_size);
    return true;
}