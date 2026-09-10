#include "platform/hash_backend.h"

#include <stdexcept>

#ifdef _WIN32

#include <windows.h>
#include <bcrypt.h>

namespace filehash {
namespace {

const wchar_t* provider_name(const HashAlgorithm algorithm) {
    switch (algorithm) {
        case HashAlgorithm::Md5: return BCRYPT_MD5_ALGORITHM;
        case HashAlgorithm::Sha1: return BCRYPT_SHA1_ALGORITHM;
        case HashAlgorithm::Sha256: return BCRYPT_SHA256_ALGORITHM;
        case HashAlgorithm::Sha384: return BCRYPT_SHA384_ALGORITHM;
        case HashAlgorithm::Sha512: return BCRYPT_SHA512_ALGORITHM;
        case HashAlgorithm::Crc32: break;
    }
    throw std::invalid_argument("CRC32 does not use a BCrypt context");
}

void check_status(const NTSTATUS status, const char* operation) {
    if (status < 0) {
        throw std::runtime_error(operation);
    }
}

class BCryptContext final : public HashContext {
public:
    explicit BCryptContext(const HashAlgorithm algorithm) {
        check_status(BCryptOpenAlgorithmProvider(&algorithm_, provider_name(algorithm),
                                                  nullptr, 0),
                     "BCryptOpenAlgorithmProvider failed");
        DWORD object_length = 0;
        DWORD result_length = 0;
        check_status(BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
                                       reinterpret_cast<PUCHAR>(&object_length),
                                       sizeof(object_length), &result_length, 0),
                     "BCryptGetProperty failed");
        object_.resize(object_length);
        check_status(BCryptCreateHash(algorithm_, &hash_, object_.data(), object_length,
                                      nullptr, 0, 0),
                     "BCryptCreateHash failed");
    }

    ~BCryptContext() override {
        if (hash_ != nullptr) BCryptDestroyHash(hash_);
        if (algorithm_ != nullptr) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }

    void update(const std::uint8_t* data, const std::size_t size) override {
        check_status(BCryptHashData(hash_, const_cast<PUCHAR>(data),
                                    static_cast<ULONG>(size), 0),
                     "BCryptHashData failed");
    }

    std::vector<std::uint8_t> finish() override {
        DWORD length = 0;
        DWORD result_length = 0;
        check_status(BCryptGetProperty(algorithm_, BCRYPT_HASH_LENGTH,
                                       reinterpret_cast<PUCHAR>(&length), sizeof(length),
                                       &result_length, 0),
                     "BCryptGetProperty hash length failed");
        std::vector<std::uint8_t> result(length);
        check_status(BCryptFinishHash(hash_, result.data(), length, 0),
                     "BCryptFinishHash failed");
        return result;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<std::uint8_t> object_;
};

}  // 命名空间 / Namespace

std::unique_ptr<HashContext> create_hash_context(const HashAlgorithm algorithm) {
    if (algorithm == HashAlgorithm::Crc32) {
        throw std::invalid_argument("CRC32 does not use a cryptographic context");
    }
    return std::make_unique<BCryptContext>(algorithm);
}

}  // 命名空间 filehash / Namespace filehash

#else

#include <openssl/evp.h>

namespace filehash {
namespace {

const EVP_MD* digest_for(const HashAlgorithm algorithm) {
    switch (algorithm) {
        case HashAlgorithm::Md5: return EVP_md5();
        case HashAlgorithm::Sha1: return EVP_sha1();
        case HashAlgorithm::Sha256: return EVP_sha256();
        case HashAlgorithm::Sha384: return EVP_sha384();
        case HashAlgorithm::Sha512: return EVP_sha512();
        case HashAlgorithm::Crc32: break;
    }
    throw std::invalid_argument("CRC32 does not use an EVP context");
}

class EvpContext final : public HashContext {
public:
    explicit EvpContext(const HashAlgorithm algorithm) {
        context_ = EVP_MD_CTX_new();
        if (context_ == nullptr || EVP_DigestInit_ex(context_, digest_for(algorithm), nullptr) != 1) {
            if (context_ != nullptr) EVP_MD_CTX_free(context_);
            throw std::runtime_error("EVP digest initialization failed");
        }
    }

    ~EvpContext() override {
        EVP_MD_CTX_free(context_);
    }

    void update(const std::uint8_t* data, const std::size_t size) override {
        if (EVP_DigestUpdate(context_, data, size) != 1) {
            throw std::runtime_error("EVP digest update failed");
        }
    }

    std::vector<std::uint8_t> finish() override {
        unsigned int size = 0;
        std::vector<std::uint8_t> result(static_cast<std::size_t>(EVP_MAX_MD_SIZE));
        if (EVP_DigestFinal_ex(context_, result.data(), &size) != 1) {
            throw std::runtime_error("EVP digest finalization failed");
        }
        result.resize(size);
        return result;
    }

private:
    EVP_MD_CTX* context_ = nullptr;
};

}  // 命名空间 / Namespace

std::unique_ptr<HashContext> create_hash_context(const HashAlgorithm algorithm) {
    if (algorithm == HashAlgorithm::Crc32) {
        throw std::invalid_argument("CRC32 does not use an EVP context");
    }
    return std::make_unique<EvpContext>(algorithm);
}

}  // 命名空间 filehash / Namespace filehash

#endif
