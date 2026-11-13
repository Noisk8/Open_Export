#pragma once

#include <QByteArray>
#include <QString>

namespace opendeck {
namespace device {
namespace onelibrary {

// SqlCipherWriter produces a SQLCipher 4 database from a plain SQLite file.
//
// Device Library Plus (`exportLibrary.db`) is an encrypted SQLite database.
// Implementing the page encryption here (with OpenSSL's libcrypto, which is
// already a system dependency) avoids vendoring SQLCipher itself.
//
// The format was reverse engineered against SQLCipher 4.13 and verified by
// opening the result with the real `sqlcipher` CLI:
//
//   salt          16 random bytes stored in the first 16 bytes of page 1
//   enc_key       PBKDF2-HMAC-SHA512(passphrase, salt, 256000, 32)
//   hmac_salt     each byte of salt XOR 0x3a
//   hmac_key      PBKDF2-HMAC-SHA512(enc_key, hmac_salt, 2, 32)
//   page layout   ciphertext || IV(16) || HMAC-SHA512(64)  = 4096 bytes
//   page 1        the first 16 bytes are the salt, not ciphertext; the
//                 encrypted area starts at byte 16 and is 4000 bytes long
//   HMAC input    ciphertext || IV || page number as u32 little endian
//
// The plain SQLite file must have page_size 4096 and 80 reserved bytes per
// page (byte 20 of the SQLite header == 80), which DeviceLibraryWriter
// arranges before calling encryptFile().
class SqlCipherWriter {
  public:
    static constexpr int kPageSize = 4096;
    static constexpr int kReservedBytes = 80;
    static constexpr int kSaltSize = 16;
    static constexpr int kIvSize = 16;
    static constexpr int kHmacSize = 64;
    static constexpr int kKeySize = 32;
    static constexpr int kKdfIterations = 256000;
    static constexpr int kFastKdfIterations = 2;

    // Passphrase used by every Device Library Plus export (not device or
    // licence dependent). It is public and documented by pyrekordbox.
    static constexpr const char* kDeviceLibraryKey =
            "r8gddnr4k847830ar6cqzbkk0el6qytmb3trbbx805jm74vez64i5o8fnrqryqls";

    // Encrypts a plain SQLite file into a SQLCipher 4 file.
    [[nodiscard]] static bool encryptFile(const QString& plainPath,
                                          const QString& cipherPath, QString* error);

    // Decrypts a SQLCipher 4 file back to plain SQLite. Used by the tests to
    // verify the round trip without needing a SQLCipher installation.
    [[nodiscard]] static bool decryptFile(const QString& cipherPath,
                                          const QString& plainPath, QString* error);

    [[nodiscard]] static QByteArray encrypt(const QByteArray& plain,
                                            const QByteArray& salt, QString* error);
    [[nodiscard]] static QByteArray decrypt(const QByteArray& cipher, QString* error);
};

} // namespace onelibrary
} // namespace device
} // namespace opendeck
