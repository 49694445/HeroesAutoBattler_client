/*
 * main.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "main.h"
#include "mainwindow_moc.h"
#include "prepare.h"

#include "../lib/VCMIDirs.h"

#include <QApplication>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>

// Conan workaround https://github.com/conan-io/conan-center-index/issues/13332
#ifdef VCMI_IOS
# if __has_include("QIOSIntegrationPlugin.h")
#  include "QIOSIntegrationPlugin.h"
# endif
int argcForClient;
char ** argvForClient;
#elif defined(VCMI_ANDROID)
# include <QAndroidJniObject>
# include <QtAndroid>
#else
# include <QMessageBox>
# include <QProcess>
#endif // VCMI_IOS

// android must export main explicitly to make it visible in the shared library
#ifdef VCMI_ANDROID
# define MAIN_EXPORT ELF_VISIBILITY
#else
# define MAIN_EXPORT
#endif // VCMI_ANDROID

int MAIN_EXPORT main(int argc, char * argv[])
{
	int result;
#ifdef VCMI_IOS
	{
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
	QApplication vcmilauncher(argc, argv);

	// use system proxy
	{
		QNetworkProxyFactory::setUseSystemConfiguration(true);
		const auto systemProxies = QNetworkProxyFactory::systemProxyForQuery();
		if(!systemProxies.isEmpty())
			QNetworkProxy::setApplicationProxy(systemProxies[0]);
	}

#if defined(VCMI_ANDROID) && defined(VCMI_REMOTE_CLIENT_ONLY)
	try
	{
#endif
	launcher::prepare();

	MainWindow mainWindow;
	mainWindow.show();

#ifdef VCMI_ANDROID
	// changing language causes window to increase size over the bounds, force it back to proper value
	// TODO: check in Qt 6 if the hack is still needed
	auto appWindow = vcmilauncher.focusWindow();
	auto resizeWindowToScreen = [appWindow]{
		appWindow->resize(appWindow->screen()->availableSize());
	};
	QObject::connect(appWindow, &QWindow::widthChanged, resizeWindowToScreen);
	QObject::connect(appWindow, &QWindow::heightChanged, resizeWindowToScreen);
#endif

	result = vcmilauncher.exec();
#if defined(VCMI_ANDROID) && defined(VCMI_REMOTE_CLIENT_ONLY)
	}
	catch(const std::exception & error)
	{
		const auto detail = QString::fromUtf8(error.what());
		const auto logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		QDir().mkpath(logDir);
		QFile log(logDir + "/startup-error.txt");
		if(log.open(QIODevice::WriteOnly | QIODevice::Append)) log.write(detail.toUtf8() + "\n");
		qCritical("Launcher startup failed: %s", error.what());
		QMessageBox::critical(nullptr, QString::fromUtf8("启动失败 / Startup failed"),
			QString::fromUtf8("启动资源安装或初始化失败，请保留应用数据。\nResource installation or initialization failed. Please keep app data.\n\n") + detail);
		result = 1;
	}
#endif
#ifdef VCMI_IOS
	}
	if (result == 0)
		launchGame(argcForClient, argvForClient);
#endif
	return result;
}

void startGame(const QStringList & args)
{
	logGlobal->warn("Starting game with the arguments: %s", args.join(" ").toStdString());

#ifdef VCMI_IOS
	static const char clientName[] = "vcmiclient";
	argcForClient = args.size() + 1; //first argument is omitted
	argvForClient = new char*[argcForClient];
	argvForClient[0] = new char[strlen(clientName)+1];
	strcpy(argvForClient[0], clientName);
	for(int i = 1; i < argcForClient; ++i)
	{
		std::string s = args.at(i - 1).toStdString();
		argvForClient[i] = new char[s.size() + 1];
		strcpy(argvForClient[i], s.c_str());
	}
	qApp->quit();
#elif defined(VCMI_ANDROID)
	QtAndroid::androidActivity().callMethod<void>("onLaunchGameBtnPressed");
#else
	startExecutable(pathToQString(VCMIDirs::get().clientPath()), args);
#endif
}

void startEditor(const QStringList & args)
{
#ifdef ENABLE_EDITOR
	startExecutable(pathToQString(VCMIDirs::get().mapEditorPath()), args);
#endif
}

#ifndef VCMI_MOBILE
void startExecutable(QString name, const QStringList & args)
{
	QProcess process;
	auto showError = [&] {
		QMessageBox::critical(qApp->activeWindow(),
			QObject::tr("Error starting executable"),
			QObject::tr("Failed to start %1\nReason: %2").arg(name, process.errorString()));
	};

#if defined(VCMI_MAC) || defined(VCMI_WINDOWS)
	if(process.startDetached(name, args))
	{
		qApp->quit();
	}
	else
	{
		showError();
	}
#else // Linux
	// Start vcmiclient and vcmieditor with QProcess::start() instead of QProcess::startDetached()
	// since startDetached() results in a missing terminal prompt after quitting vcmiclient.
	// QProcess::start() causes the launcher window to freeze while the child process is running, so we hide it in
	// MainWindow::on_startGameButton_clicked() and MainWindow::on_startEditorButton_clicked()
	process.setProcessChannelMode(QProcess::ForwardedChannels);
	process.start(name, args);
	process.waitForFinished(-1);

	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		showError();
	}

	qApp->quit();
#endif
}
#endif
