// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/str_utils.h>
#include <rpc/rpc-utils.h>

using namespace std;

int get_number(const UniValue& v)
{
    return v.isStr() ? std::stoi(v.get_str()) : v.get_int();
}

int64_t get_long_number(const UniValue& v)
{
    return v.isStr() ? std::stoll(v.get_str()) : v.get_int64();
}

int64_t get_long_number_checked(const UniValue& v, const string& sParamName)
{
    if (v.isStr())
    {
        try
        {
			return std::stoll(v.get_str());
		}
        catch (const std::exception&)
        {
			throw JSONRPCError(RPC_INVALID_PARAMETER,
                strprintf("Failed to convert parameter '%s' to number [%s]", sParamName, v.get_str()));
		}
    }
    else if (v.isNum())
    {
    	return v.get_int64();
    } else
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Invalid parameter '%s' type, expected string or number", sParamName));
}

/**
 * Convert UniValue to bool.
 * 
 * \param v - input UniValue
 * \return bool value
 * \throw JSONRPCError if the value cannot be converted to bool
 */
bool get_bool_value(const UniValue& v)
{
    bool bValue;
    bool bInvalidBool = false;
    string sValue;
    do
    {
        if (v.isBool())
        {
            bValue = v.get_bool();
            break;
        }
        if (v.isNum())
        {
            const int nValue = v.get_int();
            if (nValue == 0)
                bValue = false;
            else if (nValue == 1)
                bValue = true;
            else {
                bInvalidBool = true;
                sValue = to_string(nValue);
            }
            break;
        }
        if (v.isStr())
        {
            if (!str_tobool(v.get_str(), bValue))
            {
                bInvalidBool = true;
                sValue = v.get_str();
            }
            break;
        }
        bInvalidBool = true;
        sValue = v.getValStr();
    } while (false);
    if (bInvalidBool)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid bool value: %s", sValue));
    return bValue;
}

/**
 * Convert UniValue to double.
 * 
 * \param v - input UniValue
 * \return double value
 * \throw JSONRPCError if the value cannot be converted to double
 */
double rpc_get_double(const UniValue& v)
{
    double fValue;
    string sValue;
    bool bInvalidValue = false;
    do
    {
        if (v.isNum())
        {
            fValue = v.get_real();
            break;
        }
        if (v.isStr())
        {
            sValue = v.get_str();
            fValue = atof(sValue.c_str());
            if (fValue == 0.0f && errno == ERANGE)
            {
                bInvalidValue = true;
                break;
            }
        }
        bInvalidValue = true;
        sValue = v.getValStr();
    } while (false);
    if (bInvalidValue)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid double value: %s", sValue));
    return fValue;
}
