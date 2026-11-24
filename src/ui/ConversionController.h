#pragma once

#include <QAbstractListModel>
#include <QAtomicInteger>
#include <QList>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "ui/ConversionWorker.h"

namespace opendeck {
namespace ui {

struct ConversionEntry {
    QString source;
    QString target;
    int status = 0; // 0 pending, 1 converting, 2 done, 3 error
    QString message;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    int tagCount = 0;
    bool lossless = false;
};

// ConversionController exposes the FLAC -> WAV batch converter to QML.
// The heavy work runs on a worker thread; only this object (on the GUI
// thread) touches the model.
class ConversionController : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY countChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory NOTIFY outputDirectoryChanged)
  public:
    enum Roles {
        FileNameRole = Qt::UserRole + 1,
        SourceRole,
        TargetRole,
        StatusRole,
        MessageRole,
        SampleRateRole,
        ChannelsRole,
        BitDepthRole,
        TagCountRole,
        LosslessRole,
        ProgressLabelRole,
    };
    Q_ENUM(Roles)

    explicit ConversionController(QObject* parent = nullptr);
    ~ConversionController() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] int progress() const { return m_progress; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] int completedCount() const;
    [[nodiscard]] QString outputDirectory() const { return m_outputDirectory; }

    Q_INVOKABLE void addFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void addFolder(const QString& path);
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setOutputDirectory(const QString& path);
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

  signals:
    void runningChanged();
    void progressChanged();
    void statusTextChanged();
    void countChanged();
    void outputDirectoryChanged();
    void wavVerified(const QString& sourceHash, const QString& target, const QString& wavHash);
    void finished(int succeeded, int failed);

  private slots:
    void onItemStarted(int row, const QString& fileName);
    void onItemFinished(int row, const ConversionOutcome& outcome);
    void onWorkerFinished(int succeeded, int failed);

  private:
    void collectFlacFiles(const QString& directory, QStringList& out) const;
    void appendFile(const QString& path);
    void refreshTargets();
    void setStatus(const QString& text);

    QList<ConversionEntry> m_entries;
    QThread* m_thread = nullptr;
    QPointer<ConversionWorker> m_worker;
    QAtomicInteger<bool> m_cancelRequested{false};
    QString m_outputDirectory;
    bool m_running = false;
    int m_progress = 0;
    QString m_statusText;
};

} // namespace ui
} // namespace opendeck
