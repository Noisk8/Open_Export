#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"
#include "ui/AnalysisController.h"
#include "ui/ConversionController.h"
#include "ui/CueController.h"
#include "ui/DeviceController.h"
#include "ui/ExportController.h"
#include "ui/LibraryController.h"
#include "ui/TrackFilterModel.h"
#include "ui/PlaylistController.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenDeckExport"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenDeckExport"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QQuickStyle::setStyle(QStringLiteral("Basic")); // fully themed by our QML

    // Local library database (application-owned, not the export format).
    // ODE_LIBRARY_DB lets developers/tests point at a throwaway database
    // without touching the real library.
    QString dbPath = QString::fromLocal8Bit(qgetenv("ODE_LIBRARY_DB"));
    if (dbPath.isEmpty()) {
        const QString dataDir =
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir); // the AppDataLocation may not exist yet
        dbPath = dataDir + QStringLiteral("/library.db");
    } else {
        QDir().mkpath(QFileInfo(dbPath).absolutePath());
    }

    opendeck::library::DatabaseManager dbManager(dbPath);
    {
        QString error;
        if (!dbManager.open(&error)) {
            qWarning("Failed to open library database at %s: %s",
                    qPrintable(dbPath), qPrintable(error));
            return 1;
        }
    }

    opendeck::ui::LibraryController library(dbManager);
    opendeck::ui::PlaylistController playlists(dbManager);
    QObject::connect(&library, &opendeck::ui::LibraryController::tracksRemoved,
            &playlists, &opendeck::ui::PlaylistController::reload);
    opendeck::ui::TrackFilterModel tracks(library, playlists);
    opendeck::ui::DeviceController devices;
    opendeck::ui::CueController cues(dbManager);
    opendeck::ui::ConversionController converter;
    QObject::connect(&converter, &opendeck::ui::ConversionController::wavVerified, &library,
            [&dbManager](const QString& sourceHash, const QString& path, const QString& wavHash) {
                opendeck::library::TrackRepository repository(dbManager);
                if (!repository.registerVerifiedWav(sourceHash, path, wavHash))
                    qWarning("Could not associate a verified WAV with the library");
            });
    opendeck::ui::AnalysisController analysis(dbManager);
    opendeck::ui::ExportController exporter(playlists);

    // Repair paths whose volume was remounted under a different label before
    // doing anything else, and retry whenever a removable device appears.
    library.relocateMissing();

    // The analysis runs on a worker thread; refresh the table once it ends.
    // bindToLibrary() makes sure newly imported tracks become pending.
    analysis.bindToLibrary(library);
    QObject::connect(&analysis, &opendeck::ui::AnalysisController::finished,
            &library, [&library](int, int) { library.refresh(); });
    QObject::connect(&devices, &opendeck::ui::DeviceController::devicesChanged,
            &library, [&library]() { library.relocateMissing(); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("tracks"), &tracks);
    engine.rootContext()->setContextProperty(QStringLiteral("playlists"), &playlists);
    engine.rootContext()->setContextProperty(QStringLiteral("devices"), &devices);
    engine.rootContext()->setContextProperty(QStringLiteral("cues"), &cues);
    engine.rootContext()->setContextProperty(QStringLiteral("converter"), &converter);
    engine.rootContext()->setContextProperty(QStringLiteral("analysis"), &analysis);
    engine.rootContext()->setContextProperty(QStringLiteral("exporter"), &exporter);
    engine.loadFromModule("OpenDeckExport", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
