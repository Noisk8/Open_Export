#pragma once

#include <QAtomicInteger>
#include <QList>
#include <QObject>
#include <QPair>

#include "audio/AnalysisResult.h"

namespace opendeck {
namespace ui {

// AnalysisWorker runs the (CPU bound) decoding and signal processing off the
// GUI thread. It never touches the database: results are emitted back to the
// controller, which persists them on the main thread.
class AnalysisWorker : public QObject {
    Q_OBJECT
  public:
    explicit AnalysisWorker(QObject* parent = nullptr);

    void setTracks(const QList<QPair<qint64, QString>>& tracks);
    // The flag belongs to the controller and outlives the worker.
    void setCancelFlag(QAtomicInteger<bool>* flag) { m_cancelFlag = flag; }

  public slots:
    void run();

  signals:
    void resultReady(qint64 trackId, const opendeck::audio::AnalysisResult& result);
    void progress(int current, int total, const QString& title);
    void finished(int analyzed, int failed);

  private:
    QList<QPair<qint64, QString>> m_tracks;
    QAtomicInteger<bool>* m_cancelFlag = nullptr;
};

} // namespace ui
} // namespace opendeck
