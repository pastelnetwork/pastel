// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#pragma once
#include <vector>
#include <optional>
#include <tuple>

#include <script/scripttype.h>
#include <txdb/addressindex.h>
#include <txdb/spentindex.h>
#include <txdb/timestampindex.h>
#include <txdb/fundstransferindex.h>

using CAddressUnspentDbEntry = std::pair<CAddressUnspentKey, CAddressUnspentValue>;
using CAddressIndexDbEntry = std::pair<CAddressIndexKey, CAmount>;
using CSpentIndexDbEntry = std::pair<CSpentIndexKey, CSpentIndexValue>;
using CFundsTransferDbEntry = std::pair<CFundsTransferIndexKey, CFundsTransferIndexValue>;
using address_t = std::pair<uint160, ScriptType>;
using address_opt_t = std::optional<address_t>;

using address_unspent_vector_t = std::vector<CAddressUnspentDbEntry>;
using address_index_vector_t = std::vector<CAddressIndexDbEntry>;
using spent_index_vector_t = std::vector<CSpentIndexDbEntry>;
using address_vector_t = std::vector<address_t>;
using funds_transfer_vector_t = std::vector<CFundsTransferDbEntry>;
