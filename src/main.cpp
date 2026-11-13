#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QStandardPaths>

#include "library/DatabaseManager.h"
#include "ui/AnalysisController.h"
#include "ui/ConversionController.h"
#include "ui/CueController.h"
#include "ui/DeviceController.h"
#include "ui/ExportController.h"
#include "ui/LibraryController.h"
#include "ui/PlaylistController.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenDeckExport"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenDeckExport"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QQuickStyle::setStyle(QStringLiteral("Basic")); // fully themed by our QML

    // Local library database (application-owned, not the export format).
    const QString dataDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir); // the AppDataLocation may not exist yet
    const QString dbPath = dataDir + QStringLiteral("/library.db");

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
    opendeck::ui::DeviceController devices;
    opendeck::ui::CueController cues(dbManager);
    opendeck::ui::ConversionController converter;
    opendeck::ui::AnalysisController analysis(dbManager);
    opendeck::ui::ExportController exporter(playlists);

    // The analysis runs on a worker thread; refresh the table once it ends.
    QObject::connect(&analysis, &opendeck::ui::AnalysisController::finished,
            &library, [&library](int, int) { library.refresh(); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
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
