#include "Address.hpp"

namespace libzcash {

std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const SaplingExtendedSpendingKey &sk) const {
    return std::make_pair("sapling", sk.DefaultAddress());
}
std::pair<std::string, PaymentAddress> AddressInfoFromSpendingKey::operator()(const InvalidEncoding&) const {
    throw std::invalid_argument("Cannot derive default address from invalid spending key");
}

std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const SaplingExtendedFullViewingKey &sk) const {
    return std::make_pair("sapling", sk.DefaultAddress());
}
std::pair<std::string, PaymentAddress> AddressInfoFromViewingKey::operator()(const InvalidEncoding&) const {
    throw std::invalid_argument("Cannot derive default address from invalid viewing key");
}

}

bool IsValidPaymentAddress(const libzcash::PaymentAddress& zaddr) {
    return !std::holds_alternative<libzcash::InvalidEncoding>(zaddr);
}

bool IsValidViewingKey(const libzcash::ViewingKey& vk) {
    return !std::holds_alternative<libzcash::InvalidEncoding>(vk);
}

bool IsValidSpendingKey(const libzcash::SpendingKey& zkey) {
    return !std::holds_alternative<libzcash::InvalidEncoding>(zkey);
}