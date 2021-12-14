#pragma once

namespace json_tests
{
constexpr auto TEST_BASE58_KEYS_VALID = R"(
[
    [
        "Ptq6hqeeAXta25PGaKHs1ymktHbEb8ugxeG",
        "76a914f23b483b147a905138c83e275d463b5b6248789e88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "PtkqegiGBYiKjGorBWW78i6dgXCHaYY7mdE",
        "76a914c381e81c5cc2979c7947031f532b542d1fd6d1fb88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ",
        "76a914d8866ad2f6d4d3e198dcc0b2d48c34d7e42fb06388ac",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "ttYUyuR8GznjUNkUvnuwtTFETGuDyVYyR8W",
        "a9145f46aae3d901b0bb02afa2577781c9eeced4ebbf87",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "5Kd3NBUAdUnhyzenEwVLy9pBKxSwXvE9FMPyR4UKZvpe6E3AgLr",
        "eddbdc1168f1daeadbd3e44c1e3f8f5a284c2029f78ad26af98583a499de5b19",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "Kz6UJmQACJmLtaQj5A3JAge4kVTNQ8gbvXuwbmCj7bsaabudb3RD",
        "55c9bccb9ed68446d1b75273bbce89d7fe013a8acd1625514420fb2aca1a21c4",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "9213qJab2HNEpMpYNBa7wHGFKKbkDn24jpANDs2huN3yi4J11ko",
        "36cb93b9ab1bdabf7fb9f2c04f1b9cc879933530ae7842398eef5a63a56800c2",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "cTpB4YiyKiBcPxnefsDpbnDxFDffjqJob8wGCEDXxgQ7zQoMXJdH",
        "b9f4892c9e8282028fea1d2667c4dc5213564d41fc5783896a0d843fc15089f3",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "PtiScdavG18NkzWqYYEKp1QMQ31ttWHR7u6",
        "76a914a9365f9c4dc82438ba5fb0f09b254b5d2052ec3c88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "ptEg3T6LmUjonhxHzU419tbVXkoRycNGLZ8",
        "a91409cf6485e28ffe2d72e210d74de7db7356fe741e87",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ",
        "76a914d8866ad2f6d4d3e198dcc0b2d48c34d7e42fb06388ac",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "ttYUyuR8GznjUNkUvnuwtTFETGuDyVYyR8W",
        "a9145f46aae3d901b0bb02afa2577781c9eeced4ebbf87",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "5K494XZwps2bGyeL71pWid4noiSNA2cfCibrvRWqcHSptoFn7rc",
        "a326b95ebae30164217d7a7f57d72ab2b54e3be64928a19da0210b9568d4015e",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "L1RrrnXkcKut5DEMwtDthjwRcTTwED36thyL1DebVrKuwvohjMNi",
        "7d998b45c219a1e38e99e7cbd312ef67f77a455a9b50c730c27f02c6f730dfb4",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "93DVKyFYwSN6wEo3E2fCrFPUp17FtrtNi2Lf7n4G3garFb16CRj",
        "d6bca256b5abc5602ec2e1c121a08b0da2556587430bcf7e1898af2224885203",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "cTDVKtMGVYWTHCb1AFjmVbEbWjvKpKqKgMaR3QJxToMSQAhmCeTN",
        "a81ca4e8f90181ec4b61b6a7eb998af17b2cb04de8a03b504b9e34c4c61db7d9",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "PtiScdavG18NkzWqYYEKp1QMQ31ttWHR7u6",
        "76a914a9365f9c4dc82438ba5fb0f09b254b5d2052ec3c88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "ptEg3T6LmUjonhxHzU419tbVXkoRycNGLZ8",
        "a91409cf6485e28ffe2d72e210d74de7db7356fe741e87",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ",
        "76a914d8866ad2f6d4d3e198dcc0b2d48c34d7e42fb06388ac",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "ttYUyuR8GznjUNkUvnuwtTFETGuDyVYyR8W",
        "a9145f46aae3d901b0bb02afa2577781c9eeced4ebbf87",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "5KaBW9vNtWNhc3ZEDyNCiXLPdVPHCikRxSBWwV9NrpLLa4LsXi9",
        "e75d936d56377f432f404aabb406601f892fd49da90eb6ac558a733c93b47252",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "L1axzbSyynNYA8mCAhzxkipKkfHtAXYF4YQnhSKcLV8YXA874fgT",
        "8248bd0375f2f75d7e274ae544fb920f51784480866b102384190b1addfbaa5c",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "927CnUkUbasYtDwYwVn2j8GdTuACNnKkjZ1rpZd2yBB1CLcnXpo",
        "44c4f6a096eac5238291a94cc24c01e3b19b8d8cef72874a079e00a242237a52",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "cUcfCMRjiQf85YMzzQEk9d1s5A4K7xL5SmBCLrezqXFuTVefyhY7",
        "d1de707020a9059d6d3abaf85e17967c6555151143db13dbb06db78df0f15c69",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "PtiScdavG18NkzWqYYEKp1QMQ31ttWHR7u6",
        "76a914a9365f9c4dc82438ba5fb0f09b254b5d2052ec3c88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "ptEg3T6LmUjonhxHzU419tbVXkoRycNGLZ8",
        "a91409cf6485e28ffe2d72e210d74de7db7356fe741e87",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ",
        "76a914d8866ad2f6d4d3e198dcc0b2d48c34d7e42fb06388ac",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "ttYUyuR8GznjUNkUvnuwtTFETGuDyVYyR8W",
        "a9145f46aae3d901b0bb02afa2577781c9eeced4ebbf87",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "5HtH6GdcwCJA4ggWEL1B3jzBBUB8HPiBi9SBc5h9i4Wk4PSeApR",
        "091035445ef105fa1bb125eccfb1882f3fe69592265956ade751fd095033d8d0",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "L2xSYmMeVo3Zek3ZTsv9xUrXVAmrWxJ8Ua4cw8pkfbQhcEFhkXT8",
        "ab2b4bcdfc91d34dee0ae2a8c6b6668dadaeb3a88b9859743156f462325187af",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "92xFEve1Z9N8Z641KQQS7ByCSb8kGjsDzw6fAmjHN1LZGKQXyMq",
        "b4204389cef18bbe2b353623cbf93e8678fbc92a475b664ae98ed594e6cf0856",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "cVM65tdYu1YK37tNoAyGoJTR13VBYFva1vg9FLuPAsJijGvG6NEA",
        "e7b230133f1b5489843260236b06edca25f66adb1be455fbd38d4010d48faeef",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "PtiScdavG18NkzWqYYEKp1QMQ31ttWHR7u6",
        "76a914a9365f9c4dc82438ba5fb0f09b254b5d2052ec3c88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "ptEg3T6LmUjonhxHzU419tbVXkoRycNGLZ8",
        "a91409cf6485e28ffe2d72e210d74de7db7356fe741e87",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ",
        "76a914d8866ad2f6d4d3e198dcc0b2d48c34d7e42fb06388ac",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "ttYUyuR8GznjUNkUvnuwtTFETGuDyVYyR8W",
        "a9145f46aae3d901b0bb02afa2577781c9eeced4ebbf87",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "5KQmDryMNDcisTzRp3zEq9e4awRmJrEVU1j5vFRTKpRNYPqYrMg",
        "d1fab7ab7385ad26872237f1eb9789aa25cc986bacc695e07ac571d6cdac8bc0",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "L39Fy7AC2Hhj95gh3Yb2AU5YHh1mQSAHgpNixvm27poizcJyLtUi",
        "b0bbede33ef254e8376aceb1510253fc3550efd0fcf84dcd0c9998b288f166b3",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "91cTVUcgydqyZLgaANpf1fvL55FH53QMm4BsnCADVNYuWuqdVys",
        "037f4192c630f399d9271e26c575269b1d15be553ea1a7217f0cb8513cef41cb",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "cQspfSzsgLeiJGB2u8vrAiWpCU4MxUT6JseWo2SjXy4Qbzn2fwDw",
        "6251e205e8ad508bab5596bee086ef16cd4b239e0cc0c5d7c4e6035441e7d5de",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "PtiScdavG18NkzWqYYEKp1QMQ31ttWHR7u6",
        "76a914a9365f9c4dc82438ba5fb0f09b254b5d2052ec3c88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "ptEg3T6LmUjonhxHzU419tbVXkoRycNGLZ8",
        "a91409cf6485e28ffe2d72e210d74de7db7356fe741e87",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ",
        "76a914d8866ad2f6d4d3e198dcc0b2d48c34d7e42fb06388ac",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "ttYUyuR8GznjUNkUvnuwtTFETGuDyVYyR8W",
        "a9145f46aae3d901b0bb02afa2577781c9eeced4ebbf87",
        {
            "isPrivkey": false,
            "chain": "testnet"
        }
    ],
    [
        "5KL6zEaMtPRXZKo1bbMq7JDjjo1bJuQcsgL33je3oY8uSJCR5b4",
        "c7666842503db6dc6ea061f092cfb9c388448629a6fe868d068c42a488b478ae",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "KwV9KAfwbwt51veZWNscRTeZs9CKpojyu1MsPnaKTF5kz69H1UN2",
        "07f0803fc5399e773555ab1e8939907e9badacc17ca129e67a2f5f2ff84351dd",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "mainnet"
        }
    ],
    [
        "93N87D6uxSBzwXvpokpzg8FFmfQPmvX4xHoWQe3pLdYpbiwT5YV",
        "ea577acfb5d1d14d3b7b195c321566f12f87d2b77ea3a53f68df7ebf8604a801",
        {
            "isCompressed": false,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "cMxXusSihaX58wpJ3tNuuUcZEQGt6DKJ1wEpxys88FFaQCYjku9h",
        "0b3b34f0958d8a268193a9814da92c3e8b58b4a4378a542863e34ac289cd830c",
        {
            "isCompressed": true,
            "isPrivkey": true,
            "chain": "testnet"
        }
    ],
    [
        "PtiScdavG18NkzWqYYEKp1QMQ31ttWHR7u6",
        "76a914a9365f9c4dc82438ba5fb0f09b254b5d2052ec3c88ac",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ],
    [
        "ptEg3T6LmUjonhxHzU419tbVXkoRycNGLZ8",
        "a91409cf6485e28ffe2d72e210d74de7db7356fe741e87",
        {
            "isPrivkey": false,
            "chain": "mainnet"
        }
    ]
]
)";

};