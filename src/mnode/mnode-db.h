#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2019-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <chainparams.h>
#include <utils/fs.h>
#include <utils/util.h>
#include <utils/serialize.h>
#include <utils/streams.h>
#include <hash.h>

/** 
*   Generic Dumping and Loading
*   ---------------------------
*/

template<typename T>
class CFlatDB
{
private:
    enum class ReadResult
    {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    fs::path pathDB;
    fs::path pathDBNew;
    std::string strFilename;
    std::string strMagicMessage;

    bool Write(const T& objToSave)
    {
        // LOCK(objToSave.cs);

        const int64_t nStart = GetTimeMillis();

        // serialize, checksum data up to that point, then append checksum
        CDataStream ssObj(SER_DISK, CLIENT_VERSION);
        ssObj << strMagicMessage; // specific magic message for this type of object
        ssObj << FLATDATA(Params().MessageStart()); //-V568 network specific magic number
        ssObj << objToSave;
        const uint256 hash = Hash(ssObj.begin(), ssObj.end());
        ssObj << hash;

        pathDBNew = pathDB;
        pathDBNew.replace_extension(".new");
        // open output file, and associate with CAutoFile
        FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        const errno_t err = fopen_s(&file, pathDBNew.string().c_str(), "wb");
#else
        file = fopen(pathDBNew.string().c_str(), "wb");
#endif
        CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
        if (fileout.IsNull())
            return error("%s: Failed to open file %s", __func__, pathDBNew.string());

        // Write and commit header, data
        try
        {
            fileout << ssObj;
        } catch (const std::exception &e)
        {
            return error("%s: Serialize or I/O error - %s", __func__, e.what());
        }
        fileout.fclose();

        LogFnPrintf("Written info to %s  %dms", strFilename, GetTimeMillis() - nStart);
        LogFnPrintf("     %s", objToSave.ToString());

        return true;
    }

    ReadResult Read(T& objToLoad, std::string &error, bool fDryRun = false)
    {
        //LOCK(objToLoad.cs);

        error.clear();
        const int64_t nStart = GetTimeMillis();
        // open input file, and associate with CAutoFile
        FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        const errno_t err = fopen_s(&file, pathDB.string().c_str(), "rb");
#else
        file = fopen(pathDB.string().c_str(), "rb");
#endif
        CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
        if (filein.IsNull())
        {
            error = strprintf("Failed to open file %s", pathDB.string());
            return ReadResult::FileError;
        }

        // use file size to size memory buffer
        const auto nFileSize = fs::file_size(pathDB);
        size_t nDataSize = nFileSize >= sizeof(uint256) ? nFileSize - sizeof(uint256) : 0;
        v_uint8 vchData;
        vchData.resize(nDataSize);
        uint256 hashIn;

        // read data and checksum from file
        try {
            filein.read((char *)&vchData[0], nDataSize);
            filein >> hashIn;
        }
        catch (const std::exception &e)
        {
            error = strprintf("Deserialize or I/O error - %s", e.what());
            return ReadResult::HashReadError;
        }
        filein.fclose();

        CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

        // verify stored checksum matches input data
        uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
        if (hashIn != hashTmp)
        {
            error = "Checksum mismatch, data corrupted";
            return ReadResult::IncorrectHash;
        }

        unsigned char pchMsgTmp[4];
        std::string strMagicMessageTmp;
        try {
            // de-serialize file header (file specific magic message) and ..
            ssObj >> strMagicMessageTmp;

            // ... verify the message matches predefined one
            if (strMagicMessage != strMagicMessageTmp)
            {
                error = "Invalid magic message";
                return ReadResult::IncorrectMagicMessage;
            }


            // de-serialize file header (network specific magic number) and ..
            ssObj >> FLATDATA(pchMsgTmp);

            // ... verify the network matches ours
            if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
            {
                error = "Invalid network magic number";
                return ReadResult::IncorrectMagicNumber;
            }

            // de-serialize data into T object
            ssObj >> objToLoad;
        }
        catch (const std::exception &e)
        {
            objToLoad.Clear();
            error = strprintf("Deserialize or I/O error at pos %zu/%zu - %s", 
                ssObj.getReadPos(), nDataSize, e.what());
            return ReadResult::IncorrectFormat;
        }

        LogFnPrintf("Loaded info from %s  %dms", strFilename, GetTimeMillis() - nStart);
        LogFnPrintf("     %s", objToLoad.ToString());
        if (!fDryRun)
        {
            LogFnPrintf("Cleaning...");
            objToLoad.CheckAndRemove();
            LogFnPrintf("     %s", objToLoad.ToString());
        }

        return ReadResult::Ok;
    }


public:
    CFlatDB(const std::string &strFilenameIn, const std::string &strMagicMessageIn)
    {
        strFilename = strFilenameIn;
        pathDB = GetDataDir() / strFilenameIn;

        strMagicMessage = strMagicMessageIn;
    }

    std::string getFilePath() const noexcept
    {
        return pathDB.string();
    }

    bool Load(T& objToLoad)
    {
        std::string error;
        LogFnPrintf("Reading info from %s...", strFilename);
        const ReadResult readResult = Read(objToLoad, error);
        if (readResult == ReadResult::FileError)
            LogFnPrintf("Missing file %s, will try to recreate", strFilename);
        else if (readResult != ReadResult::Ok)
        {
            error = strprintf("Error reading %s. %s. ", strFilename, error);
            if (readResult == ReadResult::IncorrectFormat)
                error += "Magic is ok, but data has invalid format, will try to recreate";
            else
                error += "File format is unknown or invalid, please fix it manually";
            LogFnPrintf(error);
            if (readResult != ReadResult::IncorrectFormat)
                return false;
        }
        return true;
    }

    bool Dump(const T& objToSave, const bool bCheckPrevFileFormat)
    {
        std::string error;
        const int64_t nStart = GetTimeMillis();

        if (bCheckPrevFileFormat)
        {
            LogFnPrintf("Verifying [%s] format...", pathDB.string());
            T tmpObjToLoad;
            const ReadResult readResult = Read(tmpObjToLoad, error, true);

            // there was an error and it was not an error on file opening => do not proceed
            if (readResult == ReadResult::FileError)
                LogFnPrintf("Missing file %s, will try to recreate", strFilename);
            else if (readResult != ReadResult::Ok)
            {
                error = strprintf("Error reading %s. %s. ", strFilename, error);
                if (readResult == ReadResult::IncorrectFormat)
                    error += "Magic is ok, but data has invalid format, will try to recreate";
                else
                    error += "File format is unknown or invalid, please fix it manually";
                LogFnPrintf(error);
                if (readResult != ReadResult::IncorrectFormat)
                    return false;
            }
        }

        LogFnPrintf("Writing [%s]...", pathDB.string());
        if (Write(objToSave))
        {
            try
            {
                bool bBackup = false;
                fs::path pathDBbackup = pathDB;
                pathDBbackup.replace_extension(".bak");
                if (fs::exists(pathDB))
                {
                    fs::rename(pathDB, pathDBbackup);
                    bBackup = true;
                }
                fs::rename(pathDBNew, pathDB);
                if (bBackup)
                    fs::remove(pathDBbackup);
                LogFnPrintf("%s dump finished, %dms", strFilename, GetTimeMillis() - nStart);
            } catch (const std::exception& ex)
            {
                LogFnPrintf("Error writing to file [%s]. %s", pathDB.string(), ex.what());
                return false;
            }
        }

        return true;
    }
};
