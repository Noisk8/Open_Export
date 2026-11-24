#pragma once

#include <QHash>

#include "export/ExportModel.h"

namespace opendeck::device {

struct ExtensionSettings {
    struct Tag {
        QString category;
        QString name;
        QList<qint64> trackIds;
    };
    struct BankSlot {
        int slot;
        qint64 trackId;
        int cueNumber;
        int loopNumber;
    };
    struct Bank {
        QString name;
        QList<BankSlot> slotsByPosition;
    };

    QList<Tag> tags;
    QList<Bank> banks;
    static ExtensionSettings read(const QString& path, const QList<core::Track>& tracks);
};

QByteArray buildExportExt(const QList<ExportTag>& tags);
bool validateExportExt(const QByteArray& bytes, const ExportLibrary& expected);

} // namespace opendeck::device
