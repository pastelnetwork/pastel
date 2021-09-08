#pragma once
#include "zcash/address/sapling.hpp"
#include "zcash/address/zip32.h"

#include <variant>

namespace libzcash {

class InvalidEncoding {
public:
    friend bool operator==(const InvalidEncoding &a, const InvalidEncoding &b) { return true; }
    friend bool operator<(const InvalidEncoding &a, const InvalidEncoding &b) { return true; }
};

typedef std::variant<InvalidEncoding, SaplingPaymentAddress> PaymentAddress;
typedef std::variant<InvalidEncoding, SaplingExtendedFullViewingKey> ViewingKey;
typedef std::variant<InvalidEncoding, SaplingExtendedSpendingKey> SpendingKey;

class AddressInfoFromSpendingKey {
public:
    std::pair<std::string, PaymentAddress> operator()(const struct SaplingExtendedSpendingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const InvalidEncoding&) const;
};

class AddressInfoFromViewingKey {
public:
    std::pair<std::string, PaymentAddress> operator()(const struct SaplingExtendedFullViewingKey&) const;
    std::pair<std::string, PaymentAddress> operator()(const InvalidEncoding&) const;
};

} // namespace libzcash

/** Check whether a PaymentAddress is not an InvalidEncoding. */
bool IsValidPaymentAddress(const libzcash::PaymentAddress& zaddr);

/** Check whether a ViewingKey is not an InvalidEncoding. */
bool IsValidViewingKey(const libzcash::ViewingKey& vk);

/** Check whether a SpendingKey is not an InvalidEncoding. */
bool IsValidSpendingKey(const libzcash::SpendingKey& zkey);

