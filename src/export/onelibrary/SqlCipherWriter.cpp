#include "export/onelibrary/SqlCipherWriter.h"

#include <QFile>
#include <QRandomGenerator>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace opendeck {
namespace device {
namespace onelibrary {

namespace {

using Key = std::array<unsigned char, SqlCipherWriter::kKeySize>;

bool deriveKeys(const QByteArray& salt, Key* encryptionKey, Key* hmacKey, QString* error) {
    if (salt.size() != SqlCipherWriter::kSaltSize) {
        if (error) {
            *error = QStringLiteral("El salt de SQLCipher debe tener 16 bytes");
        }
        return false;
    }
    const QByteArray passphrase = QByteArray::fromRawData(
            SqlCipherWriter::kDeviceLibraryKey,
            static_cast<int>(std::strlen(SqlCipherWriter::kDeviceLibraryKey)));

    // Encryption key: PBKDF2-HMAC-SHA512 over the passphrase with the salt.
    if (PKCS5_PBKDF2_HMAC(passphrase.constData(), passphrase.size(),
                reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(),
                SqlCipherWriter::kKdfIterations, EVP_sha512(),
                encryptionKey->size(), encryptionKey->data()) != 1) {
        if (error) {
            *error = QStringLiteral("No se pudo derivar la clave de cifrado");
        }
        return false;
    }

    // HMAC key: a fast second derivation from the encryption key, with a salt
    // whose bytes are XORed with 0x3a.
    QByteArray hmacSalt = salt;
    for (char& byte : hmacSalt) {
        byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x3a);
    }
    if (PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(encryptionKey->data()),
                encryptionKey->size(),
                reinterpret_cast<const unsigned char*>(hmacSalt.constData()),
                hmacSalt.size(), SqlCipherWriter::kFastKdfIterations, EVP_sha512(),
                hmacKey->size(), hmacKey->data()) != 1) {
        if (error) {
            *error = QStringLiteral("No se pudo derivar la clave HMAC");
        }
        return false;
    }
    return true;
}

bool aesCrypt(const unsigned char* input, int length, const Key& key,
              const unsigned char* iv, bool encrypt, unsigned char* output) {
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) {
        return false;
    }
    bool ok = EVP_CipherInit_ex(context, EVP_aes_256_cbc(), nullptr, key.data(), iv,
                                encrypt ? 1 : 0) == 1;
    // The page body is always a multiple of the AES block size: no padding.
    ok = ok && EVP_CIPHER_CTX_set_padding(context, 0) == 1;
    int written = 0;
    ok = ok && EVP_CipherUpdate(context, output, &written, input, length) == 1;
    int finalWritten = 0;
    ok = ok && EVP_CipherFinal_ex(context, output + written, &finalWritten) == 1;
    ok = ok && (written + finalWritten == length);
    EVP_CIPHER_CTX_free(context);
    return ok;
}

QByteArray pageHmac(const Key& hmacKey, const QByteArray& ciphertext,
                    const QByteArray& iv, quint32 pageNumber) {
    QByteArray data = ciphertext;
    data.append(iv);
    for (int i = 0; i < 4; ++i) {
        data.append(char((pageNumber >> (8 * i)) & 0xff));
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    HMAC(EVP_sha512(), hmacKey.data(), hmacKey.size(),
            reinterpret_cast<const unsigned char*>(data.constData()),
            static_cast<size_t>(data.size()), digest, &length);
    return QByteArray(reinterpret_cast<const char*>(digest),
            static_cast<int>(std::min<unsigned int>(length, SqlCipherWriter::kHmacSize)));
}

QByteArray randomBytes(int count) {
    QByteArray bytes(count, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), count) != 1) {
        // Fall back to Qt's generator if the OpenSSL RNG is unavailable.
        auto* generator = QRandomGenerator::global();
        for (int i = 0; i < count; ++i) {
            bytes[i] = char(generator->generate() & 0xff);
        }
    }
    return bytes;
}

} // namespace

QByteArray SqlCipherWriter::encrypt(const QByteArray& plain, const QByteArray& salt,
                                    QString* error) {
    if (plain.size() < kPageSize || (plain.size() % kPageSize) != 0) {
        if (error) {
            *error = QStringLiteral("El SQLite de origen no tiene páginas de 4096 bytes");
        }
        return {};
    }
    if (static_cast<unsigned char>(plain.at(20)) != kReservedBytes) {
        if (error) {
            *error = QStringLiteral("El SQLite de origen no reserva 80 bytes por página");
        }
        return {};
    }

    Key encryptionKey{};
    Key hmacKey{};
    if (!deriveKeys(salt, &encryptionKey, &hmacKey, error)) {
        return {};
    }

    QByteArray out;
    out.reserve(plain.size());
    const int pages = plain.size() / kPageSize;
    for (int i = 0; i < pages; ++i) {
        const quint32 pageNumber = static_cast<quint32>(i + 1);
        const int start = i * kPageSize;
        // Page 1 keeps the 16 byte salt in clear; the SQLite magic is restored
        // on decryption.
        const int bodyStart = pageNumber == 1 ? kSaltSize : 0;
        const int bodyEnd = kPageSize - kReservedBytes;
        const int bodyLength = bodyEnd - bodyStart;
        const QByteArray body = plain.mid(start + bodyStart, bodyLength);

        const QByteArray iv = randomBytes(kIvSize);
        QByteArray ciphertext(bodyLength, 0);
        if (!aesCrypt(reinterpret_cast<const unsigned char*>(body.constData()), bodyLength,
                    encryptionKey, reinterpret_cast<const unsigned char*>(iv.constData()),
                    true, reinterpret_cast<unsigned char*>(ciphertext.data()))) {
            if (error) {
                *error = QStringLiteral("Fallo al cifrar la página %1").arg(pageNumber);
            }
            return {};
        }
        const QByteArray mac = pageHmac(hmacKey, ciphertext, iv, pageNumber);
        if (pageNumber == 1) {
            out.append(salt);
        }
        out.append(ciphertext);
        out.append(iv);
        out.append(mac);
    }
    return out;
}

QByteArray SqlCipherWriter::decrypt(const QByteArray& cipher, QString* error) {
    if (cipher.size() < kPageSize || (cipher.size() % kPageSize) != 0) {
        if (error) {
            *error = QStringLiteral("El fichero SQLCipher no tiene páginas de 4096 bytes");
        }
        return {};
    }
    const QByteArray salt = cipher.left(kSaltSize);
    Key encryptionKey{};
    Key hmacKey{};
    if (!deriveKeys(salt, &encryptionKey, &hmacKey, error)) {
        return {};
    }

    QByteArray out;
    out.reserve(cipher.size());
    const int pages = cipher.size() / kPageSize;
    for (int i = 0; i < pages; ++i) {
        const quint32 pageNumber = static_cast<quint32>(i + 1);
        const int start = i * kPageSize;
        const int bodyLength = (pageNumber == 1)
                ? (kPageSize - kReservedBytes - kSaltSize)
                : (kPageSize - kReservedBytes);
        const QByteArray ciphertext = cipher.mid(start + (pageNumber == 1 ? kSaltSize : 0),
                bodyLength);
        const QByteArray iv = cipher.mid(start + (pageNumber == 1 ? kSaltSize : 0)
                        + bodyLength, kIvSize);
        const QByteArray mac = cipher.mid(start + (pageNumber == 1 ? kSaltSize : 0)
                        + bodyLength + kIvSize, kHmacSize);
        if (mac != pageHmac(hmacKey, ciphertext, iv, pageNumber)) {
            if (error) {
                *error = QStringLiteral("HMAC inválido en la página %1").arg(pageNumber);
            }
            return {};
        }

        QByteArray body(bodyLength, 0);
        if (!aesCrypt(reinterpret_cast<const unsigned char*>(ciphertext.constData()),
                    bodyLength, encryptionKey,
                    reinterpret_cast<const unsigned char*>(iv.constData()), false,
                    reinterpret_cast<unsigned char*>(body.data()))) {
            if (error) {
                *error = QStringLiteral("Fallo al descifrar la página %1").arg(pageNumber);
            }
            return {};
        }
        if (pageNumber == 1) {
            out.append("SQLite format 3\0", 16);
        }
        out.append(body);
        out.append(QByteArray(kReservedBytes, 0));
    }
    return out;
}

bool SqlCipherWriter::encryptFile(const QString& plainPath, const QString& cipherPath,
                                  QString* error) {
    QFile input(plainPath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = input.errorString();
        }
        return false;
    }
    const QByteArray plain = input.readAll();
    input.close();
    const QByteArray cipher = encrypt(plain, randomBytes(kSaltSize), error);
    if (cipher.isEmpty()) {
        return false;
    }
    QFile output(cipherPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = output.errorString();
        }
        return false;
    }
    if (output.write(cipher) != cipher.size()) {
        if (error) {
            *error = output.errorString();
        }
        return false;
    }
    return true;
}

bool SqlCipherWriter::decryptFile(const QString& cipherPath, const QString& plainPath,
                                  QString* error) {
    QFile input(cipherPath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = input.errorString();
        }
        return false;
    }
    const QByteArray cipher = input.readAll();
    input.close();
    const QByteArray plain = decrypt(cipher, error);
    if (plain.isEmpty()) {
        return false;
    }
    QFile output(plainPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = output.errorString();
        }
        return false;
    }
    return output.write(plain) == plain.size();
}

} // namespace onelibrary
} // namespace device
} // namespace opendeck
