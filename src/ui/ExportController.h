#pragma once

#include <QAtomicInteger>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

namespace opendeck {
namespace ui {

class ExportWorker;
class PlaylistController;

// ExportController drives the export from QML without blocking the GUI.
//
// The library description is assembled on the GUI thread (it needs the
// database and takes milliseconds); the slow writing, validation and file
// copies happen on a worker thread. QML gets progress and can cancel.
class ExportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<qint64> selectedPlaylists MEMBER m_selectedPlaylists NOTIFY optionsChanged)
    Q_PROPERTY(bool allPlaylists MEMBER m_allPlaylists NOTIFY optionsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    // Fase 7: OneLibrary y exportación incremental.
    Q_PROPERTY(bool writeDeviceLibrary READ writeDeviceLibrary WRITE setWriteDeviceLibrary
               NOTIFY optionsChanged)
    Q_PROPERTY(QString extensionFile MEMBER m_extensionFile NOTIFY optionsChanged)
    Q_PROPERTY(bool xdjRxProfile MEMBER m_xdjRxProfile NOTIFY optionsChanged)
    Q_PROPERTY(bool useWavForFlac READ useWavForFlac WRITE setUseWavForFlac NOTIFY optionsChanged)
    Q_PROPERTY(bool incremental READ incremental WRITE setIncremental NOTIFY optionsChanged)
  public:
    explicit ExportController(PlaylistController& playlists, QObject* parent = nullptr);
    ~ExportController() override;

    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] int progress() const { return m_progress; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] bool writeDeviceLibrary() const { return m_writeDeviceLibrary; }
    [[nodiscard]] bool incremental() const { return m_incremental; }
    void setWriteDeviceLibrary(bool enabled);
    void setIncremental(bool enabled);
    bool useWavForFlac() const { return m_xdjRxProfile || m_useWavForFlac; }
    void setUseWavForFlac(bool enabled);

    Q_INVOKABLE QList<qint64> selectedTrackIds() const;
    Q_INVOKABLE QVariantMap summary(const QVariant& destination) const;
    Q_INVOKABLE void exportTo(const QVariant& destination);
    Q_INVOKABLE void cancel();

  signals:
    void runningChanged();
    void progressChanged();
    void statusTextChanged();
    void optionsChanged();
    void finished(bool ok, const QString& message);

  private slots:
    void onProgress(int current, int total, const QString& fileName);
    void onWorkerFinished(bool ok, const QString& message, bool cancelled);

  private:
    void setStatus(const QString& text);

    PlaylistController& m_playlists;
    QThread* m_thread = nullptr;
    // QPointer clears itself when the worker is destroyed by deleteLater,
    // avoiding a dangling pointer in cancel().
    QPointer<ExportWorker> m_worker;
    QAtomicInteger<bool> m_cancelRequested{false};
    QList<qint64> m_selectedPlaylists;
    bool m_allPlaylists = true;
    bool m_running = false;
    int m_progress = 0;
    bool m_writeDeviceLibrary = true;
    bool m_incremental = false;
    bool m_useWavForFlac = false;
    bool m_xdjRxProfile = false;
    QString m_statusText;
    QString m_extensionFile;
};

} // namespace ui
} // namespace opendeck
