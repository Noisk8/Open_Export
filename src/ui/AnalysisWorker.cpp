#include "ui/AnalysisWorker.h"

#include "audio/AudioAnalysisService.h"

namespace opendeck {
namespace ui {

AnalysisWorker::AnalysisWorker(QObject* parent) : QObject(parent) {
}

void AnalysisWorker::setTracks(const QList<QPair<qint64, QString>>& tracks) {
    m_tracks = tracks;
}

void AnalysisWorker::run() {
    int analyzed = 0;
    int failed = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_cancelFlag != nullptr && m_cancelFlag->loadRelaxed()) {
            break;
        }
        const auto& item = m_tracks.at(i);
        emit progress(i, m_tracks.size(), item.second);
        const audio::AnalysisResult result = audio::AudioAnalysisService::analyze(item.second, m_cancelFlag);
        if (m_cancelFlag && m_cancelFlag->loadRelaxed()) break;
        if (result.ok) {
            ++analyzed;
        } else {
            ++failed;
        }
        emit resultReady(item.first, result);
    }
    emit finished(analyzed, failed);
}

} // namespace ui
} // namespace opendeck
