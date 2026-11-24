#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStorageInfo>
#include <filesystem>
#include <functional>
#include <stdexcept>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace opendeck::device {

class ExportPublication {
public:
    explicit ExportPublication(const QString& root)
        : m_root(root), m_work(root + QStringLiteral("/.open-deck-export")),
          m_lock(lockPath(root)), m_volume(root) {
        m_lock.setStaleLockTime(0);
        if (!m_lock.tryLock()) fail("El destino está ocupado por otra exportación");
        if (QFileInfo(m_work).isSymLink() || QFileInfo(m_work + "/owner").isSymLink())
            fail("Directorio de recuperación inválido");
        if (QDir(m_work).exists() && !QFileInfo(m_work + "/owner").isFile()
                && !QDir(m_work).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
            fail("El directorio temporal de exportación está ocupado");
        recover();
        if (!QDir().mkpath(m_work)) fail("No se pudo crear la zona de preparación");
        QFile owner(m_work + "/owner");
        if (!owner.open(QIODevice::WriteOnly)) fail("No se pudo reservar la zona de preparación");
    }

    ~ExportPublication() {
        if (m_cleanupAllowed && sameVolume() && !QFileInfo(m_work).isSymLink()
                && !QFile::exists(journalPath()))
            QDir(m_work).removeRecursively();
    }

    QString stage() const {
        if (!sameVolume()) fail("El volumen de destino fue desconectado");
        return checkedPath(m_work, QStringLiteral("/stage"));
    }

    QString destination(const QString& path) const {
        if (!sameVolume()) fail("El volumen de destino fue desconectado");
        if (!path.startsWith('/') || path != QDir::cleanPath(path)
                || !(path.startsWith("/Contents/") || path.startsWith("/PIONEER/"))) {
            fail("Ruta inválida en la exportación");
        }
        return checkedPath(m_root, path);
    }

    void publish(const QStringList& removePaths, const std::function<void()>& validate) {
        QStringList paths;
        const QString staged = stage();
        QDirIterator files(staged, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) paths.append(files.next().mid(staged.size()));
        // Keep the old databases visible while the new media is being installed.
        paths.sort();
        for (const QString& path : {QStringLiteral("/PIONEER/rekordbox/export.pdb"),
                                   QStringLiteral("/PIONEER/rekordbox/exportLibrary.db"),
                                   QStringLiteral("/PIONEER/rekordbox/exportExt.pdb"),
                                   QStringLiteral("/PIONEER/rekordbox/.open-deck-manifest.json")}) {
            if (paths.removeOne(path) || removePaths.contains(path)) paths.append(path);
        }
        QJsonArray entries;
        for (const QString& path : paths) {
            const QFileInfo target(destination(path));
            if (target.exists() && !target.isFile()) fail("El destino contiene un archivo incompatible");
            entries.append(QJsonObject{{"path", path}, {"previous", target.exists()},
                                      {"install", QFile::exists(checkedPath(staged, path))}});
        }
        saveJournal(entries);
        syncVolume();
        try {
            for (const auto& value : entries) {
                const auto entry = value.toObject();
                const QString path = entry.value("path").toString();
                const QString target = destination(path);
                if (entry.value("previous").toBool()) move(target, backup(path));
                if (entry.value("install").toBool()) move(checkedPath(staged, path), target);
            }
            validate();
            finishJournal();
        } catch (...) {
            if (QFile::exists(journalPath())) recover();
            throw;
        }
    }

private:
    [[noreturn]] static void fail(const char* message) { throw std::runtime_error(message); }

    // Keep the lock off the USB. Unplugging it must not strand a lock owned
    // by this still-running app on the drive.
    static QString lockPath(const QString& root) {
        const auto hash = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256);
        return QDir::temp().filePath(QStringLiteral("open-deck-export-%1.lock")
                .arg(QString::fromLatin1(hash.toHex())));
    }

    static QString checkedPath(const QString& base, const QString& path) {
        QString current = base;
        if (QFileInfo(current).isSymLink()) fail("El destino contiene un enlace simbólico");
        for (const QString& component : path.split('/', Qt::SkipEmptyParts)) {
            current += '/' + component;
            if (QFileInfo(current).isSymLink()) fail("El destino contiene un enlace simbólico");
        }
        return current;
    }

    QString journalPath() const { return m_work + QStringLiteral("/journal.json"); }
    QString backup(const QString& path) const { return checkedPath(m_work, "/backup" + path); }

    bool sameVolume() const {
        const QStorageInfo current(m_root);
        return current.isReady() && current.isValid() && current.device() == m_volume.device()
                && current.rootPath() == m_volume.rootPath();
    }

    static void move(const QString& from, const QString& to) {
        if (!QDir().mkpath(QFileInfo(to).absolutePath()))
            fail("No se pudo crear una carpeta de la exportación");
        std::error_code error;
        std::filesystem::rename(QFileInfo(from).filesystemFilePath(),
                                QFileInfo(to).filesystemFilePath(), error);
        if (error) fail("No se pudo mover un archivo de la exportación");
    }

    void syncVolume() const {
        if (!sameVolume()) fail("El volumen de destino fue desconectado");
#ifdef Q_OS_LINUX
        const int fd = ::open(QFile::encodeName(m_root).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) fail("No se pudo abrir el volumen para confirmar los cambios");
        const int result = ::syncfs(fd);
        ::close(fd);
        if (result != 0) fail("El sistema no pudo confirmar la escritura en el volumen");
#endif
    }

    void finishJournal() {
        syncVolume();
        m_cleanupAllowed = false;
        if (!QFile::remove(journalPath())) fail("No se pudo cerrar el registro de recuperación");
        syncVolume();
        m_cleanupAllowed = true;
    }

    void saveJournal(const QJsonArray& entries) {
        QSaveFile file(journalPath());
        const auto data = QJsonDocument(entries).toJson(QJsonDocument::Compact);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            fail("No se pudo guardar el registro de recuperación");
    }

    void recover() {
        if (!sameVolume()) fail("El volumen de destino fue desconectado");
        QFile journal(checkedPath(m_work, QStringLiteral("/journal.json")));
        if (journal.exists()) {
            if (!journal.open(QIODevice::ReadOnly)) fail("No se pudo abrir el registro de recuperación");
            const auto data = QJsonDocument::fromJson(journal.readAll());
            journal.close();
            if (!data.isArray()) fail("Registro de recuperación inválido");
            const auto entries = data.array();
            for (qsizetype i = entries.size(); i > 0; --i) {
                const auto entry = entries.at(i - 1).toObject();
                const QString path = entry.value("path").toString();
                const QString target = destination(path);
                const bool hasBackup = QFile::exists(backup(path));
                const bool installedNew = !entry.value("previous").toBool()
                        && entry.value("install").toBool()
                        && !QFile::exists(checkedPath(stage(), path));
                if ((hasBackup || installedNew) && QFile::exists(target) && !QFile::remove(target))
                    fail("No se pudo recuperar la exportación anterior");
                if (hasBackup) move(backup(path), target);
            }
            finishJournal();
        }
        if (QFileInfo::exists(m_work) && !QDir(m_work).removeRecursively())
            fail("No se pudo limpiar el directorio temporal de exportación");
    }

    QString m_root;
    QString m_work;
    QLockFile m_lock;
    QStorageInfo m_volume;
    bool m_cleanupAllowed = true;
};

} // namespace opendeck::device
