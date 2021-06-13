#pragma once
// Copyright (c) 2018-2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include "primitives/transaction.h"
#include "serialize.h"

class CDataStream;

const int kDefaultZSTDCompressLevel = 19;

class CDataCompressor
{
private:
    CDataStream &data;
protected:
    bool Compress(std::vector<unsigned char> &out) const;
    bool Decompress(const std::vector<unsigned char> &out);

public:
    CDataCompressor(CDataStream &dataIn) : data(dataIn) { }

    template<typename Stream>
    bool Serialize(Stream &s) const {
        std::vector<unsigned char> compr;
        uint8_t dumpbyte = 0; // for reserve now - is 0 now, TODO: support compression type
        if (Compress(compr)) {
            s << dumpbyte;
            s << VARINT(compr.size());
            s << CFlatData(compr);
            return true;
        }
       
       return false;
    }

    template<typename Stream>
    bool Unserialize(Stream &s) {
        unsigned int nSize = 0;
        uint8_t dumpbyte; // for reserve now - is 0 now, TODO: support compression type

        s >> dumpbyte;
        if (dumpbyte != 0) {
            return false;
        }
    
        // TODO: validate size with max allowed size
        s >> VARINT(nSize);

        // Read compressed data
        std::vector<unsigned char> vch(nSize, 0x00);
        s >> REF(CFlatData(vch));

        // Decompress
        return Decompress(vch);
    }
};

