#pragma once

#include <QAtomicInteger>
#include <QObject>
#include <QPointer>
#include <QString>

#include "audio/AnalysisResult.h"

namespace opendeck {
namespace library {
class DatabaseManager;
}

namespace ui {

class AnalysisWorker;

// AnalysisController drives the analysis of the local library from QML.
//
// It owns a worker thread, forwards progress to the UI and writes the results
// into the SQLite library on the GUI thread (the database connection is not
// shared across threads).
class AnalysisController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingCountChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool ffmpegAvailable READ ffmpegAvailable CONSTANT)
  public:
    explicit AnalysisController(library::DatabaseManager& db, QObject* parent = nullptr);
    ~AnalysisController() override;

    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] int pendingCount() const { return m_pendingCount; }
    [[nodiscard]] int progress() const { return m_progress; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] bool ffmpegAvailable() const;

    // Analyses every track that is not yet analysed.
    Q_INVOKABLE void analyzeAll();
    Q_INVOKABLE void cancel();

  signals:
    void runningChanged();
    void pendingCountChanged();
    void progressChanged();
    void statusTextChanged();
    void finished(int analyzed, int failed);
    void trackAnalyzed(qint64 trackId);

  private slots:
    void onResult(qint64 trackId, const audio::AnalysisResult& result);
    void onProgress(int current, int total, const QString& title);
    void onWorkerFinished(int analyzed, int failed);

  private:
    void updatePendingCount();
    void setStatus(const QString& text);

    library::DatabaseManager& m_db;
    QThread* m_thread = nullptr;
    QPointer<AnalysisWorker> m_worker;
    QAtomicInteger<bool> m_cancelRequested{false};
    bool m_running = false;
    int m_pendingCount = 0;
    int m_progress = 0;
    QString m_statusText;
};

} // namespace ui
} // namespace opendeck
