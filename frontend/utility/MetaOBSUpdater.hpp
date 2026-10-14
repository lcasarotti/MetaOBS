#pragma once

#include "MetaOBSRelease.hpp"

#include <QObject>
#include <QPointer>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

class OBSBasic;
class QProgressDialog;
class QThread;

namespace MetaOBS {

/* Updates MetaOBS from its GitHub releases. It checks the latest release,
 * downloads its Windows package and verifies it against the digest GitHub
 * reports, then hands it to an install script that waits for MetaOBS to
 * close, installs the update and starts MetaOBS again. A copy installed with
 * the installer is updated by running the new installer silently; any other
 * copy, by copying the files of the zip package over it.
 *
 * Transfers use libcurl on a worker thread; dialogs stay on the UI thread. */
class Updater : public QObject {
	Q_OBJECT

public:
	explicit Updater(OBSBasic *main);
	~Updater();

	bool IsBusy() const { return busy; }
	void Check(bool manual);

	/* Tells the user how the update installed before this start went. */
	static void ReportInstallResult();

signals:
	void Finished();

private:
	void ReleaseReceived(bool manual, bool found, const std::string &text, const std::string &error);
	void OfferRelease(bool manual);
	void Download();
	void DownloadProgress(int percent);
	void DownloadFinished(bool canceled, const QString &error);
	bool Install(QString &error);
	void Finish();
	void RunWorker(std::function<void()> work);
	void JoinWorker();

	OBSBasic *main;
	bool busy = false;
	bool useInstaller = false;
	Release release;
	QString packagePath;
	QPointer<QProgressDialog> progress;
	std::shared_ptr<std::atomic<bool>> cancel;
	QThread *worker = nullptr;
};

} // namespace MetaOBS
