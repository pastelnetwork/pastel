#include "json_test_vectors.h"

UniValue read_json(const std::string& jsondata)
{
    UniValue v;

    if (!(v.read(jsondata) && v.isArray()))
    {
        ADD_FAILURE();
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}

UniValue read_json(const char *szJsonData)
{
    UniValue v;

    if (!(v.read(szJsonData) && v.isArray())) {
        ADD_FAILURE();
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}
