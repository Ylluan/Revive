#include "trayiconcontroller.h"
#include "openvroverlaycontroller.h"
#include "revivemanifestcontroller.h"
#include "windowsservices.h"
#include <qt_windows.h>
#include <winsparkle.h>

#include <QApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQmlContext>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

QFile* g_LogFile = nullptr;

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	Q_UNUSED(context);
	QByteArray localMsg = msg.toLocal8Bit();
	QTextStream log(g_LogFile);
	log << localMsg.constData() << "\n";
	OutputDebugStringA(localMsg.constData());
	OutputDebugStringA("\n");

	if (type == QtFatalMsg)
		abort();
}

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);

	// Open the log file and install our handler.
	QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	logPath.append("/Revive");
	if (QDir().mkpath(logPath)) {
		g_LogFile = new QFile(logPath + "/ReviveOverlay.txt");
		g_LogFile->open(QIODevice::WriteOnly | QIODevice::Truncate);

		// Remove obsolete log files
		QDir logDir(logPath);
		logDir.remove("ReviveOverlay.log");
		logDir.remove("ReviveInjector.log");
	}
	qInstallMessageHandler(myMessageOutput);

	// Handle command-line arguments
	if (a.arguments().contains("-manifest")) {
		// Only initialize the manifest
		vr::EVRInitError err = vr::VRInitError_None;
		vr::VR_Init( &err, vr::VRApplication_Utility );

		if ( err != vr::VRInitError_None )
			return -1;

		QString filePath = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/app.vrmanifest");
		vr::VRApplications()->AddApplicationManifest(qPrintable(filePath));
		vr::VRApplications()->SetApplicationAutoLaunch(CReviveManifestController::AppKey, true);
		vr::VR_Shutdown();
		return 0;
	}

	if (COpenVROverlayController::SharedInstance()->Init())
	{
		// If the dashboard was successfully created keep running in the background
		a.setQuitOnLastWindowClosed(false);

		if (!CTrayIconController::SharedInstance()->Init())
			qDebug("Failed to initialize the tray icon");
	}

	if (!CReviveManifestController::SharedInstance()->Init())
		qDebug("Failed to initialize the revive manifest");

	// Create a QML engine.
	QQmlEngine qmlEngine;
	qmlEngine.rootContext()->setContextProperty("Revive", CReviveManifestController::SharedInstance());
	qmlEngine.rootContext()->setContextProperty("OpenVR", COpenVROverlayController::SharedInstance());

	QQmlComponent qmlComponent( &qmlEngine, QUrl("qrc:/Overlay.qml"));
	if (qmlComponent.isError())
	{
		qDebug(qUtf8Printable(qmlComponent.errorString()));
		return -1;
	}

	QObject *rootObject = qmlComponent.create();
	QQuickItem *rootItem = qobject_cast<QQuickItem*>( rootObject );

	COpenVROverlayController::SharedInstance()->SetQuickItem( rootItem );

	win_sparkle_set_appcast_url("https://raw.githubusercontent.com/LibreVR/Revive/master/appcast.xml");
	win_sparkle_set_can_shutdown_callback([]() { return (BOOL)!QApplication::startingUp(); });
	win_sparkle_set_shutdown_request_callback([]() { CTrayIconController::SharedInstance()->quit(); });
	win_sparkle_init();
	QObject::connect(&a, &QApplication::aboutToQuit, win_sparkle_cleanup);
	return a.exec();
}
