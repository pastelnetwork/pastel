// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#pragma once
#include <vector>

#include <txdb/addressindex.h>
#include <txdb/burntxindex.h>
#include <txdb/spentindex.h>
#include <txdb/timestampindex.h>

using CAddressUnspentDbEntry = std::pair<CAddressUnspentKey, CAddressUnspentValue>;
using CAddressIndexDbEntry = std::pair<CAddressIndexKey, CAmount>;
using CSpentIndexDbEntry = std::pair<CSpentIndexKey, CSpentIndexValue>;
using CBurnTxIndexDbEntry = std::pair<CBurnTxIndexKey, CBurnTxIndexValue>;

using address_unspent_vector_t = std::vector<CAddressUnspentDbEntry>;
using address_index_vector_t = std::vector<CAddressIndexDbEntry>;
using burn_txindex_vector_t = std::vector<CBurnTxIndexDbEntry>;
using spent_index_vector_t = std::vector<CSpentIndexDbEntry>;
