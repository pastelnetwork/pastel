#pragma once
// Copyright (c) 2018-2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include "primitives/transaction.h"
#include "serialize.h"
#include <stdexcept>
class CDataStream;

const int kDefaultZSTDCompressLevel = 19;

class CDataCompressor
{
private:
    CDataStream &data;
protected:
    int Compress(std::vector<unsigned char> &out) const;
    int Decompress(const std::vector<unsigned char> &out);
    static const char* getErrorStr(int errCode);

public:
    CDataCompressor(CDataStream &dataIn) : data(dataIn) { }

    template<typename Stream>
    void Serialize(Stream &s) const {
        std::vector<unsigned char> compr;
        uint8_t dumpbyte = 0; // for reserve now - is 0 now, TODO: support compression type
        int ret;

        ret = Compress(compr);
        if (ret >= 0) {
            s << dumpbyte;
            s << VARINT(compr.size());
            s << CFlatData(compr);
            return;
        }

        throw std::runtime_error(strprintf("compress error:  %s", getErrorStr(ret)));
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nSize = 0;
        int ret;
        uint8_t dumpbyte; // for reserve now - is 0 now, TODO: support compression type

        s >> dumpbyte;
        if (dumpbyte != 0) {
            return;
        }
    
        // TODO: validate size with max allowed size
        s >> VARINT(nSize);

        // Read compressed data
        std::vector<unsigned char> vch(nSize, 0x00);
        s >> REF(CFlatData(vch));

        // Decompress
        ret = Decompress(vch);
        if (ret < 0) {
            throw std::runtime_error(strprintf("decompress error: %s", getErrorStr(ret)));
        }
    }
};

