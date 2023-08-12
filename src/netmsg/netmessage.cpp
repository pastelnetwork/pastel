// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <netmsg/netmessage.h>

CNetMessage::CNetMessage(const CMessageHeader::MessageStartChars& pchMessageStartIn, const int nTypeIn, const int nVersionIn) : 
    hdrbuf(nTypeIn, nVersionIn),
    hdr(pchMessageStartIn),
    vRecv(nTypeIn, nVersionIn)
{
    hdrbuf.resize(24);
    in_data = false;
    nHdrPos = 0;
    nDataPos = 0;
    nTime = 0;
}

int CNetMessage::readHeader(const char *pch, unsigned int nBytes)
{
    // copy data to temporary parsing buffer
    unsigned int nRemaining = 24 - nHdrPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24)
        return nCopy;

    // deserialize to CMessageHeader
    try {
        hdrbuf >> hdr;
    }
    catch (const std::exception&) {
        return -1;
    }

    // reject messages larger than MAX_DATA_SIZE
    if (hdr.nMessageSize > MAX_DATA_SIZE)
            return -1;

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char *pch, unsigned int nBytes)
{
    unsigned int nRemaining = hdr.nMessageSize - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy)
    {
        // Allocate up to 256 KiB ahead, but never more than the total message size.
        vRecv.resize(std::min(hdr.nMessageSize, nDataPos + nCopy + 256 * 1024));
    }

    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}

bool CNetMessage::complete() const noexcept
{
    if (!in_data)
        return false;
    return (hdr.nMessageSize == nDataPos);
}

void CNetMessage::SetVersion(int nVersionIn) noexcept
{
    hdrbuf.SetVersion(nVersionIn);
    vRecv.SetVersion(nVersionIn);
}
