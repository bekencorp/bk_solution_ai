/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <xplat/noise/core/PsaCryptoBackend.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace musegadgets::noise::core {
namespace {

constexpr uint8_t kX25519LowByteClampMask = 0xf8;
constexpr uint8_t kX25519HighByteClearMask = 0x7f;
constexpr uint8_t kX25519HighByteSetMask = 0x40;

bool IsValidInput(ConstByteSpan bytes) noexcept {
  return bytes.data() != nullptr || bytes.empty();
}

bool IsValidOutput(ByteSpan bytes) noexcept {
  return bytes.data() != nullptr || bytes.empty();
}

bool HasExactInputSize(ConstByteSpan bytes, size_t expected) noexcept {
  return bytes.size() == expected && IsValidInput(bytes);
}

bool HasExactOutputSize(ByteSpan bytes, size_t expected) noexcept {
  return bytes.size() == expected && IsValidOutput(bytes);
}

void SecureZeroize(ByteSpan bytes) noexcept {
  volatile uint8_t* p = bytes.data();
  for (size_t i = 0; p != nullptr && i < bytes.size(); ++i) {
    p[i] = 0;
  }
}

template <size_t kSize>
void SecureZeroize(std::array<uint8_t, kSize>& bytes) noexcept {
  SecureZeroize(ByteSpan(bytes));
}

Status MbedStatusToStatus(int status) noexcept {
  if (status == 0) {
    return OkStatus();
  }
  if (status == MBEDTLS_ERR_GCM_AUTH_FAILED) {
    return Status::Unauthenticated();
  }
  if (status == MBEDTLS_ERR_ECP_BAD_INPUT_DATA ||
      status == MBEDTLS_ERR_ECP_INVALID_KEY ||
      status == MBEDTLS_ERR_MPI_BAD_INPUT_DATA) {
    return Status::InvalidArgument();
  }
  if (status == MBEDTLS_ERR_MPI_ALLOC_FAILED ||
      status == MBEDTLS_ERR_ECP_ALLOC_FAILED) {
    return Status::ResourceExhausted();
  }
  if (status == MBEDTLS_ERR_ENTROPY_SOURCE_FAILED ||
      status == MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED) {
    return Status::Unavailable();
  }
  return Status::Internal();
}

class RandomContext {
 public:
  RandomContext() noexcept {
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&drbg_);
    static constexpr unsigned char kPersonalization[] =
        "Muse Noise_XX_25519_AESGCM_SHA256";
    result_ = mbedtls_ctr_drbg_seed(
        &drbg_,
        mbedtls_entropy_func,
        &entropy_,
        kPersonalization,
        sizeof(kPersonalization) - 1);
  }

  ~RandomContext() noexcept {
    mbedtls_ctr_drbg_free(&drbg_);
    mbedtls_entropy_free(&entropy_);
  }

  RandomContext(const RandomContext&) = delete;
  RandomContext& operator=(const RandomContext&) = delete;

  int result() const noexcept {
    return result_;
  }

  int Generate(unsigned char* output, size_t size) noexcept {
    if (result_ != 0) {
      return result_;
    }
    while (size != 0) {
      const size_t chunk = std::min(size, size_t{MBEDTLS_CTR_DRBG_MAX_REQUEST});
      const int status = mbedtls_ctr_drbg_random(&drbg_, output, chunk);
      if (status != 0) {
        return status;
      }
      output += chunk;
      size -= chunk;
    }
    return 0;
  }

  static int GenerateCallback(
      void* context,
      unsigned char* output,
      size_t size) noexcept {
    return static_cast<RandomContext*>(context)->Generate(output, size);
  }

 private:
  mbedtls_entropy_context entropy_;
  mbedtls_ctr_drbg_context drbg_;
  int result_{MBEDTLS_ERR_ENTROPY_SOURCE_FAILED};
};

void ClampX25519PrivateKey(
    ConstByteSpan privateKey32,
    std::array<uint8_t, CryptoBackend::kX25519KeySize>& out) noexcept {
  std::copy_n(privateKey32.data(), out.size(), out.data());
  out[0] &= kX25519LowByteClampMask;
  out[31] &= kX25519HighByteClearMask;
  out[31] |= kX25519HighByteSetMask;
}

int ReadX25519PrivateKey(
    ConstByteSpan privateKey32,
    mbedtls_mpi* privateKey) noexcept {
  std::array<uint8_t, CryptoBackend::kX25519KeySize> clamped{};
  ClampX25519PrivateKey(privateKey32, clamped);
  const int status =
      mbedtls_mpi_read_binary_le(privateKey, clamped.data(), clamped.size());
  SecureZeroize(clamped);
  return status;
}

const uint8_t* MbedData(ConstByteSpan bytes) noexcept {
  static constexpr uint8_t kEmptyData = 0;
  return bytes.empty() ? &kEmptyData : bytes.data();
}

uint8_t* MbedData(ByteSpan bytes) noexcept {
  static uint8_t kEmptyData = 0;
  return bytes.empty() ? &kEmptyData : bytes.data();
}

bool IsAllZero(ConstByteSpan bytes) noexcept {
  uint8_t anyBitsSet = 0;
  for (size_t i = 0; i < bytes.size(); ++i) {
    anyBitsSet = static_cast<uint8_t>(anyBitsSet | bytes[i]);
  }
  return anyBitsSet == 0;
}

bool RangesOverlap(
    const uint8_t* firstData,
    size_t firstSize,
    const uint8_t* secondData,
    size_t secondSize) noexcept {
  if (firstSize == 0 || secondSize == 0 ||
      firstData == nullptr || secondData == nullptr) {
    return false;
  }
  const uintptr_t first = reinterpret_cast<uintptr_t>(firstData);
  const uintptr_t second = reinterpret_cast<uintptr_t>(secondData);
  return first <= second
      ? second - first < firstSize
      : first - second < secondSize;
}

bool IsExactInPlace(ConstByteSpan input, ByteSpan output) noexcept {
  return input.data() == output.data();
}

Status ValidateAesInputs(
    ConstByteSpan key32,
    ConstByteSpan nonce12,
    ConstByteSpan aad) noexcept {
  if (!HasExactInputSize(key32, CryptoBackend::kAes256GcmKeySize) ||
      !HasExactInputSize(nonce12, CryptoBackend::kAes256GcmNonceSize) ||
      !IsValidInput(aad)) {
    return Status::InvalidArgument();
  }
  return OkStatus();
}

Status HmacSha256(
    ConstByteSpan key,
    ConstByteSpan first,
    ConstByteSpan second,
    ConstByteSpan third,
    ByteSpan digest32) noexcept {
  if (!HasExactOutputSize(digest32, CryptoBackend::kSha256DigestSize)) {
    return Status::InvalidArgument();
  }

  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    return Status::Unavailable();
  }
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  int result = mbedtls_md_setup(&context, info, 1);
  if (result == 0) {
    result = mbedtls_md_hmac_starts(&context, MbedData(key), key.size());
  }
  if (result == 0 && !first.empty()) {
    result = mbedtls_md_hmac_update(&context, first.data(), first.size());
  }
  if (result == 0 && !second.empty()) {
    result = mbedtls_md_hmac_update(&context, second.data(), second.size());
  }
  if (result == 0 && !third.empty()) {
    result = mbedtls_md_hmac_update(&context, third.data(), third.size());
  }
  if (result == 0) {
    result = mbedtls_md_hmac_finish(&context, digest32.data());
  }
  mbedtls_md_free(&context);
  if (result != 0) {
    SecureZeroize(digest32);
  }
  return MbedStatusToStatus(result);
}

} // namespace

Status PsaCryptoBackend::Random(ByteSpan out) noexcept {
  if (!IsValidOutput(out)) {
    return Status::InvalidArgument();
  }
  if (out.empty()) {
    return OkStatus();
  }
  RandomContext random;
  const int result = random.Generate(out.data(), out.size());
  if (result != 0) {
    SecureZeroize(out);
  }
  return MbedStatusToStatus(result);
}

Status PsaCryptoBackend::X25519GenerateKeypair(
    ByteSpan privateKey32,
    ByteSpan publicKey32) noexcept {
  if (!HasExactOutputSize(privateKey32, kX25519KeySize) ||
      !HasExactOutputSize(publicKey32, kX25519KeySize)) {
    return Status::InvalidArgument();
  }
  RandomContext random;
  mbedtls_ecp_group group;
  mbedtls_mpi privateKey;
  mbedtls_ecp_point publicKey;
  mbedtls_ecp_group_init(&group);
  mbedtls_mpi_init(&privateKey);
  mbedtls_ecp_point_init(&publicKey);

  int result = random.result();
  if (result == 0) {
    result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519);
  }
  if (result == 0) {
    result = mbedtls_ecdh_gen_public(
        &group,
        &privateKey,
        &publicKey,
        RandomContext::GenerateCallback,
        &random);
  }
  size_t publicKeyLen = 0;
  if (result == 0) {
    result = mbedtls_mpi_write_binary_le(
        &privateKey, privateKey32.data(), privateKey32.size());
  }
  if (result == 0) {
    result = mbedtls_ecp_point_write_binary(
        &group,
        &publicKey,
        MBEDTLS_ECP_PF_UNCOMPRESSED,
        &publicKeyLen,
        publicKey32.data(),
        publicKey32.size());
  }
  mbedtls_ecp_point_free(&publicKey);
  mbedtls_mpi_free(&privateKey);
  mbedtls_ecp_group_free(&group);
  if (result != 0 || publicKeyLen != kX25519KeySize) {
    SecureZeroize(privateKey32);
    SecureZeroize(publicKey32);
    return result == 0 ? Status::Internal() : MbedStatusToStatus(result);
  }
  return OkStatus();
}

Status PsaCryptoBackend::X25519PublicFromPrivate(
    ConstByteSpan privateKey32,
    ByteSpan publicKey32) noexcept {
  if (!HasExactInputSize(privateKey32, kX25519KeySize) ||
      !HasExactOutputSize(publicKey32, kX25519KeySize)) {
    return Status::InvalidArgument();
  }
  RandomContext random;
  mbedtls_ecp_group group;
  mbedtls_mpi privateKey;
  mbedtls_ecp_point publicKey;
  mbedtls_ecp_group_init(&group);
  mbedtls_mpi_init(&privateKey);
  mbedtls_ecp_point_init(&publicKey);

  int result = random.result();
  if (result == 0) {
    result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519);
  }
  if (result == 0) {
    result = ReadX25519PrivateKey(privateKey32, &privateKey);
  }
  if (result == 0) {
    result = mbedtls_ecp_mul(
        &group,
        &publicKey,
        &privateKey,
        &group.G,
        RandomContext::GenerateCallback,
        &random);
  }
  size_t publicKeyLen = 0;
  if (result == 0) {
    result = mbedtls_ecp_point_write_binary(
        &group,
        &publicKey,
        MBEDTLS_ECP_PF_UNCOMPRESSED,
        &publicKeyLen,
        publicKey32.data(),
        publicKey32.size());
  }
  mbedtls_ecp_point_free(&publicKey);
  mbedtls_mpi_free(&privateKey);
  mbedtls_ecp_group_free(&group);
  if (result != 0 || publicKeyLen != kX25519KeySize) {
    SecureZeroize(publicKey32);
    return result == 0 ? Status::Internal() : MbedStatusToStatus(result);
  }
  return OkStatus();
}

Status PsaCryptoBackend::X25519Dh(
    ConstByteSpan privateKey32,
    ConstByteSpan peerPublic32,
    ByteSpan sharedSecret32) noexcept {
  if (!HasExactInputSize(privateKey32, kX25519KeySize) ||
      !HasExactInputSize(peerPublic32, kX25519KeySize) ||
      !HasExactOutputSize(sharedSecret32, kX25519KeySize)) {
    return Status::InvalidArgument();
  }
  RandomContext random;
  mbedtls_ecp_group group;
  mbedtls_mpi privateKey;
  mbedtls_ecp_point peerPublic;
  mbedtls_mpi sharedSecret;
  mbedtls_ecp_group_init(&group);
  mbedtls_mpi_init(&privateKey);
  mbedtls_ecp_point_init(&peerPublic);
  mbedtls_mpi_init(&sharedSecret);
  std::array<uint8_t, kX25519KeySize> shared{};
  int result = random.result();
  if (result == 0) {
    result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519);
  }
  if (result == 0) {
    result = ReadX25519PrivateKey(privateKey32, &privateKey);
  }
  if (result == 0) {
    result = mbedtls_ecp_point_read_binary(
        &group, &peerPublic, peerPublic32.data(), peerPublic32.size());
  }
  if (result == 0) {
    result = mbedtls_ecdh_compute_shared(
        &group,
        &sharedSecret,
        &peerPublic,
        &privateKey,
        RandomContext::GenerateCallback,
        &random);
  }
  if (result == 0) {
    result = mbedtls_mpi_write_binary_le(
        &sharedSecret, shared.data(), shared.size());
  }
  Status status = MbedStatusToStatus(result);
  if (result == 0 && IsAllZero(ConstByteSpan(shared))) {
    status = Status::InvalidArgument();
  } else if (result == 0) {
    std::copy(shared.begin(), shared.end(), sharedSecret32.data());
  }
  mbedtls_mpi_free(&sharedSecret);
  mbedtls_ecp_point_free(&peerPublic);
  mbedtls_mpi_free(&privateKey);
  mbedtls_ecp_group_free(&group);
  SecureZeroize(shared);
  return status;
}

Status PsaCryptoBackend::Sha256(
    ConstByteSpan data,
    ByteSpan digest32) noexcept {
  if (!IsValidInput(data) || !HasExactOutputSize(digest32, kSha256DigestSize)) {
    return Status::InvalidArgument();
  }
  const int result =
      mbedtls_sha256(MbedData(data), data.size(), digest32.data(), 0);
  if (result != 0) {
    SecureZeroize(digest32);
  }
  return MbedStatusToStatus(result);
}

Status PsaCryptoBackend::Sha256Concat(
    ConstByteSpan prefix,
    ConstByteSpan suffix,
    ByteSpan digest32) noexcept {
  if (!IsValidInput(prefix) || !IsValidInput(suffix) ||
      !HasExactOutputSize(digest32, kSha256DigestSize)) {
    return Status::InvalidArgument();
  }
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  int result = mbedtls_sha256_starts(&context, 0);
  if (result == 0 && !prefix.empty()) {
    result = mbedtls_sha256_update(
        &context, prefix.data(), prefix.size());
  }
  if (result == 0 && !suffix.empty()) {
    result = mbedtls_sha256_update(
        &context, suffix.data(), suffix.size());
  }
  if (result == 0) {
    result = mbedtls_sha256_finish(&context, digest32.data());
  }
  mbedtls_sha256_free(&context);
  if (result != 0) {
    SecureZeroize(digest32);
  }
  return MbedStatusToStatus(result);
}

Status PsaCryptoBackend::HkdfSha256(
    ConstByteSpan salt,
    ConstByteSpan ikm,
    ConstByteSpan info,
    Span<ByteSpan> outputs) noexcept {
  if (!IsValidInput(salt) || !IsValidInput(ikm) || !IsValidInput(info) ||
      outputs.data() == nullptr || outputs.empty() ||
      outputs.size() > kMaxHkdfSha256Outputs) {
    return Status::InvalidArgument();
  }
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (!HasExactOutputSize(outputs[i], kSha256DigestSize)) {
      return Status::InvalidArgument();
    }
  }

  std::array<uint8_t, kSha256DigestSize> zeroSalt{};
  const ConstByteSpan hmacSalt = salt.empty() ? ConstByteSpan(zeroSalt) : salt;
  std::array<uint8_t, kSha256DigestSize> prk{};
  Status status = HmacSha256(
      hmacSalt, ikm, ConstByteSpan(), ConstByteSpan(), ByteSpan(prk));
  if (!status.ok()) {
    SecureZeroize(prk);
    return status;
  }

  std::array<uint8_t, kSha256DigestSize * kMaxHkdfSha256Outputs> okm{};
  std::array<uint8_t, 1> counter{};
  for (size_t i = 0; status.ok() && i < outputs.size(); ++i) {
    counter[0] = static_cast<uint8_t>(i + 1);
    const ConstByteSpan previous = i == 0
        ? ConstByteSpan()
        : ConstByteSpan(
              okm.data() + ((i - 1) * kSha256DigestSize), kSha256DigestSize);
    status = HmacSha256(
        ConstByteSpan(prk),
        previous,
        info,
        ConstByteSpan(counter),
        ByteSpan(okm.data() + (i * kSha256DigestSize), kSha256DigestSize));
  }
  SecureZeroize(prk);

  if (!status.ok()) {
    SecureZeroize(okm);
    return status;
  }

  for (size_t i = 0; i < outputs.size(); ++i) {
    std::copy_n(
        okm.data() + (i * kSha256DigestSize),
        kSha256DigestSize,
        outputs[i].data());
  }
  SecureZeroize(okm);
  return OkStatus();
}

Status PsaCryptoBackend::Aes256GcmSeal(
    ConstByteSpan key32,
    ConstByteSpan nonce12,
    ConstByteSpan aad,
    ConstByteSpan plaintext,
    ByteSpan ciphertextAndTagOut) noexcept {
  Status status = ValidateAesInputs(key32, nonce12, aad);
  if (!status.ok()) {
    return status;
  }
  if (!IsValidInput(plaintext) ||
      plaintext.size() > SIZE_MAX - kAes256GcmTagSize ||
      ciphertextAndTagOut.size() != plaintext.size() + kAes256GcmTagSize ||
      !IsValidOutput(ciphertextAndTagOut)) {
    return Status::InvalidArgument();
  }
  if (RangesOverlap(
          plaintext.data(),
          plaintext.size(),
          ciphertextAndTagOut.data(),
          ciphertextAndTagOut.size()) &&
      !IsExactInPlace(plaintext, ciphertextAndTagOut)) {
    return Status::InvalidArgument();
  }

  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  int result = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, key32.data(), key32.size() * 8);
  if (result == 0) {
    result = mbedtls_gcm_crypt_and_tag(
        &context,
        MBEDTLS_GCM_ENCRYPT,
        plaintext.size(),
        nonce12.data(),
        nonce12.size(),
        MbedData(aad),
        aad.size(),
        MbedData(plaintext),
        ciphertextAndTagOut.data(),
        kAes256GcmTagSize,
        ciphertextAndTagOut.data() + plaintext.size());
  }
  mbedtls_gcm_free(&context);
  if (result != 0) {
    SecureZeroize(ciphertextAndTagOut);
  }
  return MbedStatusToStatus(result);
}

Status PsaCryptoBackend::Aes256GcmOpen(
    ConstByteSpan key32,
    ConstByteSpan nonce12,
    ConstByteSpan aad,
    ConstByteSpan ciphertextAndTag,
    ByteSpan plaintextOut) noexcept {
  Status status = ValidateAesInputs(key32, nonce12, aad);
  if (!status.ok()) {
    return status;
  }
  if (!IsValidInput(ciphertextAndTag) ||
      ciphertextAndTag.size() < kAes256GcmTagSize) {
    return Status::InvalidArgument();
  }
  const size_t plaintextLen = ciphertextAndTag.size() - kAes256GcmTagSize;
  if (!HasExactOutputSize(plaintextOut, plaintextLen)) {
    return Status::InvalidArgument();
  }
  if (RangesOverlap(
          ciphertextAndTag.data(),
          ciphertextAndTag.size(),
          plaintextOut.data(),
          plaintextOut.size()) &&
      !IsExactInPlace(ciphertextAndTag, plaintextOut)) {
    return Status::InvalidArgument();
  }

  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  int result = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, key32.data(), key32.size() * 8);
  if (result == 0) {
    result = mbedtls_gcm_auth_decrypt(
        &context,
        plaintextLen,
        nonce12.data(),
        nonce12.size(),
        MbedData(aad),
        aad.size(),
        ciphertextAndTag.data() + plaintextLen,
        kAes256GcmTagSize,
        ciphertextAndTag.data(),
        MbedData(plaintextOut));
  }
  mbedtls_gcm_free(&context);
  if (result != 0) {
    SecureZeroize(plaintextOut);
  }
  return MbedStatusToStatus(result);
}

} // namespace musegadgets::noise::core
