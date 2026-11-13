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
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
  public:
    explicit ExportController(PlaylistController& playlists, QObject* parent = nullptr);
    ~ExportController() override;

    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] int progress() const { return m_progress; }
    [[nodiscard]] QString statusText() const { return m_statusText; }

    Q_INVOKABLE void exportTo(const QVariant& destination);
    Q_INVOKABLE void cancel();

  signals:
    void runningChanged();
    void progressChanged();
    void statusTextChanged();
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
    bool m_running = false;
    int m_progress = 0;
    QString m_statusText;
};

} // namespace ui
} // namespace opendeck
