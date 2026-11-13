#pragma once

#include <QAtomicInteger>
#include <QObject>
#include <QString>

#include "export/ExportManager.h"
#include "export/ExportModel.h"

namespace opendeck {
namespace ui {

// ExportWorker performs the whole export off the GUI thread: copying audio
// can take minutes on a FAT32 stick, and doing it on the GUI thread freezes
// the window. It only receives data that is already resolved; it never
// touches the SQLite connection.
class ExportWorker : public QObject {
    Q_OBJECT
  public:
    explicit ExportWorker(QObject* parent = nullptr);

    void setLibrary(const device::ExportLibrary& library) { m_library = library; }
    void setOptions(const device::ExportOptions& options) { m_options = options; }
    // The flag belongs to the controller and outlives the worker, so asking
    // for cancellation never dereferences a deleted object.
    void setCancelFlag(QAtomicInteger<bool>* flag) { m_cancelFlag = flag; }

  public slots:
    void run();

  signals:
    void progress(int current, int total, const QString& fileName);
    void finished(bool ok, const QString& message, bool cancelled);

  private:
    device::ExportLibrary m_library;
    device::ExportOptions m_options;
    QAtomicInteger<bool>* m_cancelFlag = nullptr;
};

} // namespace ui
} // namespace opendeck
