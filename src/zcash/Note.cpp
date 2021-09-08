#include "Note.hpp"
#include "prf.h"
#include "crypto/sha256.h"

#include "random.h"
#include "version.h"
#include "streams.h"

#include "zcash/util.h"
#include "librustzcash.h"

using namespace libzcash;

// Construct and populate Sapling note for a given payment address and value.
SaplingNote::SaplingNote(const SaplingPaymentAddress& address, const uint64_t value) : BaseNote(value) {
    d = address.d;
    pk_d = address.pk_d;
    librustzcash_sapling_generate_r(r.begin());
}

// Call librustzcash to compute the commitment
std::optional<uint256> SaplingNote::cm() const {
    uint256 result;
    if (!librustzcash_sapling_compute_cm(
            d.data(),
            pk_d.begin(),
            value(),
            r.begin(),
            result.begin()
        ))
    {
        return std::nullopt;
    }

    return result;
}

// Call librustzcash to compute the nullifier
std::optional<uint256> SaplingNote::nullifier(const SaplingFullViewingKey& vk, const uint64_t position) const
{
    auto ak = vk.ak;
    auto nk = vk.nk;

    uint256 result;
    if (!librustzcash_sapling_compute_nf(
            d.data(),
            pk_d.begin(),
            value(),
            r.begin(),
            ak.begin(),
            nk.begin(),
            position,
            result.begin()
    ))
    {
        return std::nullopt;
    }

    return result;
}

// Construct and populate SaplingNotePlaintext for a given note and memo.
SaplingNotePlaintext::SaplingNotePlaintext(
    const SaplingNote& note,
    std::array<unsigned char, ZC_MEMO_SIZE> memo) : 
    BaseNotePlaintext(note, memo),
    rseed(),
    leadingByte()
{
    d = note.d;
    rcm = note.r;
}


std::optional<SaplingNote> SaplingNotePlaintext::note(const SaplingIncomingViewingKey& ivk) const
{
    auto addr = ivk.address(d);
    if (addr)
        return SaplingNote(d, addr.value().pk_d, value_, rcm);
    return std::nullopt;
}

std::optional<SaplingOutgoingPlaintext> SaplingOutgoingPlaintext::decrypt(
    const SaplingOutCiphertext &ciphertext,
    const uint256& ovk,
    const uint256& cv,
    const uint256& cm,
    const uint256& epk
)
{
    auto pt = AttemptSaplingOutDecryption(ciphertext, ovk, cv, cm, epk);
    if (!pt) {
        return std::nullopt;
    }

    // Deserialize from the plaintext
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << pt.value();

    SaplingOutgoingPlaintext ret;
    ss >> ret;

    assert(ss.size() == 0);

    return ret;
}

/*
std::optional<SaplingNotePlaintext> SaplingNotePlaintext::decrypt(
    const Consensus::Params& params,
    int height,
    const SaplingEncCiphertext &ciphertext,
    const uint256 &ivk,
    const uint256 &epk,
    const uint256 &cmu
)
{
    auto ret = attempt_sapling_enc_decryption_deserialization(ciphertext, ivk, epk);

    if (!ret) {
        return std::nullopt;
    } else {
        const SaplingNotePlaintext plaintext = *ret;

        // Check leadbyte is allowed at block height
        if (!plaintext_version_is_valid(params, height, plaintext.get_leadbyte())) {
            LogPrint("receiveunsafe", "Received note plaintext with invalid lead byte %d at height %d",
                     plaintext.get_leadbyte(), height);
            return std::nullopt;
        }

        return plaintext_checks_without_height(plaintext, ivk, epk, cmu);
    }
}
*/

std::optional<SaplingNotePlaintext> SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(
    const SaplingEncCiphertext &ciphertext,
    const uint256 &ivk,
    const uint256 &epk
)
{
    auto encPlaintext = AttemptSaplingEncDecryption(ciphertext, ivk, epk);
    if (!encPlaintext)
        return std::nullopt;

    // Deserialize from the plaintext
    SaplingNotePlaintext ret;
    try
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << encPlaintext.value();
        ss >> ret;
        assert(ss.size() == 0);
        return ret;
    } catch (const boost::thread_interrupted&) {
        throw;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<SaplingNotePlaintext> SaplingNotePlaintext::plaintext_checks_without_height(
    const SaplingNotePlaintext &plaintext,
    const uint256 &ivk,
    const uint256 &epk,
    const uint256 &cmu
)
{
    /*
    uint256 pk_d;
    if (!librustzcash_ivk_to_pkd(ivk.begin(), plaintext.d.data(), pk_d.begin())) {
        return std::nullopt;
    }

    uint256 cmu_expected;
    uint256 rcm = plaintext.rcm();
    if (!librustzcash_sapling_compute_cmu(
        plaintext.d.data(),
        pk_d.begin(),
        plaintext.value(),
        rcm.begin(),
        cmu_expected.begin()
    ))
    {

        return std::nullopt;
    }

    if (cmu_expected != cmu) {
        return std::nullopt;
    }

    if (plaintext.get_leadbyte() != 0x01) {
        assert(plaintext.get_leadbyte() == 0x02);
        // ZIP 212: Check that epk is consistent to guard against linkability
        // attacks without relying on the soundness of the SNARK.
        uint256 expected_epk;
        uint256 esk = plaintext.generate_or_derive_esk();
        if (!librustzcash_sapling_ka_derivepublic(plaintext.d.data(), esk.begin(), expected_epk.begin())) {
            return std::nullopt;
        }
        if (expected_epk != epk) {
            return std::nullopt;
        }
    }

    */
    return plaintext;
}

std::optional<SaplingNotePlaintext> SaplingNotePlaintext::decrypt(
    const SaplingEncCiphertext &ciphertext,
    const uint256 &ivk,
    const uint256 &epk,
    const uint256 &cmu
)
{
    auto pt = AttemptSaplingEncDecryption(ciphertext, ivk, epk);
    if (!pt)
        return std::nullopt;

    // Deserialize from the plaintext
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << pt.value();

    SaplingNotePlaintext ret;
    ss >> ret;

    assert(ss.size() == 0);

    uint256 pk_d;
    if (!librustzcash_ivk_to_pkd(ivk.begin(), ret.d.data(), pk_d.begin()))
        return std::nullopt;

    uint256 cmu_expected;
    if (!librustzcash_sapling_compute_cm(
        ret.d.data(),
        pk_d.begin(),
        ret.value(),
        ret.rcm.begin(),
        cmu_expected.begin()))
    {
        return std::nullopt;
    }

    if (cmu_expected != cmu)
        return std::nullopt;
    return ret;
}

std::optional<SaplingNotePlaintext> SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(
    const SaplingEncCiphertext &ciphertext,
    const uint256 &epk,
    const uint256 &esk,
    const uint256 &pk_d
)
{
    auto encPlaintext = AttemptSaplingEncDecryption(ciphertext, epk, esk, pk_d);

    if (!encPlaintext)
        return std::nullopt;

    // Deserialize from the plaintext
    SaplingNotePlaintext ret;
    try
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << encPlaintext.value();
        ss >> ret;
        assert(ss.size() == 0);
        return ret;
    } catch (const boost::thread_interrupted&) {
        throw;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<SaplingNotePlaintext> SaplingNotePlaintext::plaintext_checks_without_height(
    const SaplingNotePlaintext &plaintext,
    const uint256 &epk,
    const uint256 &esk,
    const uint256 &pk_d,
    const uint256 &cmu
)
{
    /*
    if (plaintext.get_leadbyte() != 0x01) {
        assert(plaintext.get_leadbyte() == 0x02);
        // ZIP 212: Additionally check that the esk provided to this function
        // is consistent with the esk we can derive
        if (esk != plaintext.generate_or_derive_esk()) {
            return std::nullopt;
        }
    }

    // ZIP 212: The recipient MUST derive esk and check that epk is consistent with it.
    // https://zips.z.cash/zip-0212#changes-to-the-process-of-receiving-sapling-notes
    uint256 expected_epk;
    if (!librustzcash_sapling_ka_derivepublic(plaintext.d.data(), esk.begin(), expected_epk.begin())) {
        return std::nullopt;
    }
    if (expected_epk != epk) {
        return std::nullopt;
    }

    uint256 cmu_expected;
    uint256 rcm = plaintext.rcm();
    if (!librustzcash_sapling_compute_cmu(
        plaintext.d.data(),
        pk_d.begin(),
        plaintext.value(),
        rcm.begin(),
        cmu_expected.begin()
    ))
    {
        return std::nullopt;
    }

    if (cmu_expected != cmu) {
        return std::nullopt;
    }

    */
    return plaintext;
}

std::optional<SaplingNotePlaintext> SaplingNotePlaintext::decrypt(
    const SaplingEncCiphertext &ciphertext,
    const uint256 &epk,
    const uint256 &esk,
    const uint256 &pk_d,
    const uint256 &cmu
)
{
    auto pt = AttemptSaplingEncDecryption(ciphertext, epk, esk, pk_d);
    if (!pt)
        return std::nullopt;

    // Deserialize from the plaintext
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << pt.value();

    SaplingNotePlaintext ret;
    ss >> ret;

    uint256 cmu_expected;
    if (!librustzcash_sapling_compute_cm(
        ret.d.data(),
        pk_d.begin(),
        ret.value(),
        ret.rcm.begin(),
        cmu_expected.begin()))
    {
        return std::nullopt;
    }

    if (cmu_expected != cmu)
        return std::nullopt;

    assert(ss.size() == 0);
    return ret;
}

/**
 * Encrypt Sapling note.
 * 
 * \param pk_d - diversified transmission key for the intended recipient address of a Sapling note
 * \return encrypted Sapling note or std::nullopt
 */
std::optional<SaplingNotePlaintextEncryptionResult> SaplingNotePlaintext::encrypt(const uint256& pk_d) const
{
    // Get the encryptor
    auto sne = SaplingNoteEncryption::FromDiversifier(d);
    if (!sne)
        return std::nullopt;
    auto enc = sne.value();

    // Create the plaintext
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << (*this);
    SaplingEncPlaintext pt;
    assert(pt.size() == ss.size());
    memcpy(&pt[0], &ss[0], pt.size());

    // Encrypt the plaintext
    auto encciphertext = enc.encrypt_to_recipient(pk_d, pt);
    if (!encciphertext)
        return std::nullopt;
    return SaplingNotePlaintextEncryptionResult(encciphertext.value(), enc);
}


SaplingOutCiphertext SaplingOutgoingPlaintext::encrypt(
        const uint256& ovk,
        const uint256& cv,
        const uint256& cm,
        SaplingNoteEncryption& enc
    ) const
{
    // Create the plaintext
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << (*this);
    SaplingOutPlaintext pt;
    assert(pt.size() == ss.size());
    memcpy(&pt[0], &ss[0], pt.size());

    return enc.encrypt_to_ourselves(ovk, cv, cm, pt);
}
