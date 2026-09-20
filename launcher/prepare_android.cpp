/*
 * prepare_android.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "prepare_p.h"
#include "../lib/CAndroidVMHelper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QSaveFile>

#include <QAndroidJniEnvironment>
#include <QAndroidJniObject>
#include <QtAndroid>

namespace
{
// https://gist.github.com/ssendeavour/7324701
bool copyRecursively(const QString & srcFilePath, const QString & tgtFilePath)
{
	QFileInfo srcFileInfo{srcFilePath};
	if(srcFileInfo.isDir()) {
		QDir targetDir{tgtFilePath};
		targetDir.cdUp();
		if(!targetDir.mkpath(QFileInfo{tgtFilePath}.fileName()))
			return false;
		targetDir.setPath(tgtFilePath);

		QDir sourceDir{srcFilePath};
		const auto fileNames = sourceDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
		for(const auto & fileName : fileNames) {
			const auto newSrcFilePath = sourceDir.filePath(fileName);
			const auto newTgtFilePath = targetDir.filePath(fileName);
			if(!copyRecursively(newSrcFilePath, newTgtFilePath))
				return false;
		}
	} else {
		if(!QFile::copy(srcFilePath, tgtFilePath))
			return false;
	}
	return true;
}
}

namespace launcher
{
void prepareAndroid()
{
	QAndroidJniEnvironment jniEnv;
	CAndroidVMHelper::initClassloader(static_cast<JNIEnv *>(jniEnv));

	const bool justLaunched = QtAndroid::androidActivity().getField<jboolean>("justLaunched") == JNI_TRUE;
	if(!justLaunched)
		return;

	// copy core data to internal directory
	const auto vcmiDir = QAndroidJniObject::callStaticObjectMethod<jstring>("eu/vcmi/vcmi/NativeMethods", "internalDataRoot").toString();
#ifdef VCMI_REMOTE_CLIENT_ONLY
	QFile bundledId("assets:/hab-bundle/bundle-id.txt");
	if(!bundledId.open(QIODevice::ReadOnly))
		throw std::runtime_error("Bundled game resources are missing");
	const auto expectedId = bundledId.readAll();
	const QString managedRoot = vcmiDir + "/hab-bundle";
	QFile installedId(managedRoot + "/bundle-id.txt");
	const bool current = installedId.open(QIODevice::ReadOnly) && installedId.readAll() == expectedId;
	installedId.close();
	if(!current)
	{
		// Only this app-owned cache is replaced; external player files are untouched.
		QDir(managedRoot).removeRecursively();
		// Android asset directories cannot reliably be traversed through QFileInfo/QDir.
		// Use the packaged inventory and open each asset by its exact path.
		QFile manifest("assets:/hab-bundle/manifest.json");
		if(!manifest.open(QIODevice::ReadOnly))
			throw std::runtime_error("Bundled resource manifest is missing");
		const auto document = QJsonDocument::fromJson(manifest.readAll());
		if(!document.isObject() || document.object().isEmpty())
			throw std::runtime_error("Bundled resource manifest is invalid");
		const auto entries = document.object();
		for(auto entry = entries.begin(); entry != entries.end(); ++entry)
		{
			const auto relative = entry.key();
			if(!relative.startsWith("Mods/") || relative.contains("..") || relative.contains('\\'))
				throw std::runtime_error("Invalid bundled resource path");
			const auto target = managedRoot + "/" + relative;
			QFile source("assets:/hab-bundle/" + relative);
			QSaveFile output(target);
			auto fail = [&] {
				throw std::runtime_error((QString("Unable to install %1: %2 / %3")
					.arg(relative, source.errorString(), output.errorString())).toStdString());
			};
			if(!QDir().mkpath(QFileInfo(target).absolutePath()) || !source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
				fail();
			QCryptographicHash hash(QCryptographicHash::Sha256);
			while(!source.atEnd())
			{
				const auto chunk = source.read(1024 * 1024);
				if(chunk.isEmpty() || output.write(chunk) != chunk.size()) fail();
				hash.addData(chunk);
			}
			if(hash.result().toHex() != entry.value().toString().toLatin1())
				throw std::runtime_error(("Bundled resource checksum mismatch: " + relative).toStdString());
			if(!output.commit()) fail();
		}
		if(!installedId.open(QIODevice::WriteOnly) || installedId.write(expectedId) != expectedId.size())
			throw std::runtime_error("Unable to finish bundled resource installation");
	}
#endif
	for(auto vcmiFilesResource : {QLatin1String{"config"}, QLatin1String{"Mods"}})
	{
		QDir destDir = QString{"%1/%2"}.arg(vcmiDir, vcmiFilesResource);
		destDir.removeRecursively();
		copyRecursively(QString{":/%1"}.arg(vcmiFilesResource), destDir.absolutePath());
	}
}
}
