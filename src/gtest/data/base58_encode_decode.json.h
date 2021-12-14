#pragma once

namespace json_tests
{
constexpr auto TEST_BASE58_ENCODE_DECODE_JSON = R"(
[
["", ""],
["61", "2g"],
["626262", "a3gV"],
["636363", "aPEr"],
["73696d706c792061206c6f6e6720737472696e67", "2cFupjhnEsSn59qHXstmK2ffpLv2"],
["00eb15231dfceb60925886b67d065299925915aeb172c06647", "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L"],
["516b6fcd0f", "ABnLTmg"],
["bf4f89001e670274dd", "3SEo3LWLoPntC"],
["572e4794", "3EFU7m"],
["ecac89cad93923c02321", "EJDM8drfXA6uyA"],
["10c8511e", "Rt5zm"],
["00000000000000000000", "1111111111"]
]
)";

};