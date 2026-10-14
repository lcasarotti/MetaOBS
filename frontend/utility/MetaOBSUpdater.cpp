#include "MetaOBSUpdater.hpp"

#include <OBSApp.hpp>
#include <dialogs/OBSUpdate.hpp>
#include <utility/RemoteTextThread.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>
#include <util/curl/curl-helper.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QProgressDialog>
#include <QTextDocument>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "moc_MetaOBSUpdater.cpp"

namespace MetaOBS {

namespace {
constexpr const char *updatesDirectory = "obs-studio/updates/metaobs";
constexpr const char *scriptName = "metaobs-update.ps1";
constexpr const char *configName = "metaobs-update.json";
constexpr const char *resultName = "metaobs-update-result.json";
constexpr const char *logName = "metaobs-update.log";
constexpr const char *skipVersionKey = "MetaOBSSkipVersion";

constexpr int checkTimeoutSeconds = 30;
constexpr long connectTimeoutSeconds = 30;
/* A download that stays below one byte per second this long fails. */
constexpr long stalledSeconds = 60;

QString UpdatesPath(const QString &name = QString())
{
	BPtr<char> path = GetAppConfigPathPtr(updatesDirectory);
	QString directory = QT_UTF8(path.Get());
	return name.isEmpty() ? directory : directory + "/" + name;
}

QString NativePath(const QString &path)
{
	return QDir::toNativeSeparators(path);
}

/* The dialogs belong to the window the user is in. In Accessible Mode the
 * main window is hidden, so fall back to whichever main window is shown. */
QWidget *DialogParent()
{
	if (QWidget *active = QApplication::activeWindow()) {
		return active;
	}
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (widget->isVisible() && qobject_cast<QMainWindow *>(widget)) {
			return widget;
		}
	}
	return App()->GetMainWindow();
}

bool CanWrite(const QString &directory)
{
	QFile file(directory + "/.metaobs-write-test");
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	file.close();
	file.remove();
	return true;
}

/* MetaOBS starts again with the arguments it was started with. */
QString RestartArguments()
{
	QStringList arguments;
	obs_cmdline_args args = obs_get_cmdline_args();
	for (int i = 1; i < args.argc; i++) {
		QString argument = QT_UTF8(args.argv[i]);
		arguments << (argument.contains(' ') ? '"' + argument + '"' : argument);
	}

	/* Portable mode can be enabled by a file next to the program, so the
	 * command line does not always have the flag. */
	if (App()->IsPortableMode() && !arguments.contains("--portable")) {
		arguments << "--portable";
	}
	return arguments.join(' ');
}

struct DownloadState {
	QFile file;
	QCryptographicHash hash{QCryptographicHash::Sha256};
	qint64 received = 0;
	qint64 expected = 0;
	int percent = -1;
	std::atomic<bool> *cancel = nullptr;
	std::function<void(int)> report;
	QString writeError;
};

size_t WritePackage(char *data, size_t size, size_t count, void *param)
{
	auto state = static_cast<DownloadState *>(param);
	qint64 total = static_cast<qint64>(size * count);

	if (state->file.write(data, total) != total) {
		state->writeError = state->file.errorString();
		return 0;
	}
	state->hash.addData(QByteArrayView(data, total));
	state->received += total;

	if (state->expected > 0) {
		int percent = static_cast<int>(std::min<qint64>(state->received * 100 / state->expected, 100));
		if (percent != state->percent) {
			state->percent = percent;
			state->report(percent);
		}
	}
	return static_cast<size_t>(total);
}

int CheckCanceled(void *param, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<DownloadState *>(param)->cancel->load() ? 1 : 0;
}

/* Downloads the package to path and checks it against the release. Returns
 * an error message, or an empty string on success or cancellation. */
QString DownloadPackage(const Release &release, const QString &path, std::atomic<bool> &cancel,
			std::function<void(int)> report)
{
	DownloadState state;
	state.file.setFileName(path);
	state.expected = release.packageSize;
	state.cancel = &cancel;
	state.report = std::move(report);

	if (!state.file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return state.file.errorString();
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		return QStringLiteral("could not initialize libcurl");
	}

	std::string url = release.packageUrl.toStdString();
	char curlError[CURL_ERROR_SIZE] = {};

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "MetaOBS/" METAOBS_VERSION);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WritePackage);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CheckCanceled);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connectTimeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, stalledSeconds);
	curl_obs_set_revoke_setting(curl);

	CURLcode code = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	state.file.close();

	if (cancel) {
		return QString();
	}
	if (!state.writeError.isEmpty()) {
		return state.writeError;
	}
	if (code != CURLE_OK) {
		return QString::fromUtf8(curlError[0] ? curlError : curl_easy_strerror(code));
	}
	if (state.received != release.packageSize || state.hash.result() != release.packageSha256) {
		return QTStr("MetaOBS.Updater.Damaged");
	}
	return QString();
}
} // namespace

Updater::Updater(OBSBasic *main_) : QObject(main_), main(main_) {}

Updater::~Updater()
{
	if (cancel) {
		*cancel = true;
	}
	JoinWorker();
}

void Updater::RunWorker(std::function<void()> work)
{
	worker = QThread::create(std::move(work));
	worker->start();
}

void Updater::JoinWorker()
{
	if (worker) {
		worker->wait();
		delete worker;
		worker = nullptr;
	}
}

void Updater::Finish()
{
	busy = false;
	emit Finished();
}

void Updater::Check(bool manual)
{
	if (busy) {
		return;
	}
	busy = true;

	std::string url =
		QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(METAOBS_REPOSITORY).toStdString();

	RunWorker([this, manual, url]() {
		std::string text;
		std::string error;
		long responseCode = 0;
		std::vector<std::string> headers = {"Accept: application/vnd.github+json",
						    "X-GitHub-Api-Version: 2022-11-28"};

		bool found = GetRemoteFile(url.c_str(), text, error, &responseCode, nullptr, "", nullptr, headers,
					   nullptr, checkTimeoutSeconds);

		/* GitHub answers 404 while the repository has no release. */
		if (!found && responseCode == 404) {
			error.clear();
		}

		QMetaObject::invokeMethod(
			this, [this, manual, found, text, error]() { ReleaseReceived(manual, found, text, error); },
			Qt::QueuedConnection);
	});
}

void Updater::ReleaseReceived(bool manual, bool found, const std::string &text, const std::string &error)
{
	JoinWorker();

	QString checkError;
	if (!found) {
		checkError = QT_UTF8(error.c_str());
	} else if (!ParseRelease(QByteArray::fromStdString(text), windowsPackageSuffix, release, checkError)) {
		found = false;
	}

	if (!found) {
		if (checkError.isEmpty()) {
			blog(LOG_INFO, "MetaOBS update check: no release found");
			if (manual) {
				OBSMessageBox::information(DialogParent(), QTStr("Updater.NoUpdatesAvailable.Title"),
							   QTStr("Updater.NoUpdatesAvailable.Text"));
			}
		} else {
			blog(LOG_WARNING, "MetaOBS update check failed: %s", QT_TO_UTF8(checkError));
			if (manual) {
				OBSMessageBox::warning(DialogParent(), QTStr("MetaOBS.Updater.Title"),
						       QTStr("MetaOBS.Updater.CheckFailed").arg(checkError));
			}
		}
		Finish();
		return;
	}

	blog(LOG_INFO, "MetaOBS update check: latest release %s, running %s", QT_TO_UTF8(release.version),
	     METAOBS_VERSION);

	if (CompareVersions(release.version, METAOBS_VERSION) <= 0) {
		if (manual) {
			OBSMessageBox::information(DialogParent(), QTStr("Updater.NoUpdatesAvailable.Title"),
						   QTStr("Updater.NoUpdatesAvailable.Text"));
		}
		Finish();
		return;
	}

	const char *skipVersion = config_get_string(App()->GetAppConfig(), "General", skipVersionKey);
	if (!manual && skipVersion && release.version == skipVersion) {
		Finish();
		return;
	}

	OfferRelease(manual);
}

void Updater::OfferRelease(bool manual)
{
	/* Only a package that GitHub hosts and has a digest for is installed;
	 * otherwise the user can still get the release from its page. */
	bool installable = release.packageUrl.startsWith("https://github.com/") && release.packageSha256.size() == 32;
	if (!installable) {
		blog(LOG_WARNING, "MetaOBS %s has no verifiable Windows package", QT_TO_UTF8(release.version));
		if (manual) {
			auto answer = OBSMessageBox::question(DialogParent(), QTStr("MetaOBS.Updater.Title"),
							      QTStr("MetaOBS.Updater.NoPackage").arg(release.version));
			if (answer == QMessageBox::Yes) {
				QDesktopServices::openUrl(QUrl(release.pageUrl));
			}
		}
		Finish();
		return;
	}

	QTextDocument notes;
	notes.setMarkdown(QStringLiteral("## %1\n\n%2").arg(release.name, release.notes));

	OBSUpdate dialog(DialogParent(), manual, notes.toHtml());
	int result = dialog.exec();

	if (result == OBSUpdate::Yes) {
		Download();
		return;
	}

	if (result == OBSUpdate::Skip) {
		config_set_string(App()->GetAppConfig(), "General", skipVersionKey, QT_TO_UTF8(release.version));
	} else if (!manual) {
		config_set_int(App()->GetAppConfig(), "General", "LastUpdateCheck", static_cast<long long>(time(nullptr)));
	}
	Finish();
}

void Updater::Download()
{
	QDir().mkpath(UpdatesPath());
	packagePath = UpdatesPath(QStringLiteral("MetaOBS-%1%2").arg(release.version, windowsPackageSuffix));

	progress = new QProgressDialog(QTStr("MetaOBS.Updater.Downloading").arg(release.version), QTStr("Cancel"), 0,
				       100, DialogParent());
	progress->setWindowTitle(QTStr("MetaOBS.Updater.Title"));
	progress->setAttribute(Qt::WA_DeleteOnClose);
	progress->setAutoClose(false);
	progress->setAutoReset(false);
	progress->setMinimumDuration(0);
	progress->setValue(0);
	progress->show();

	cancel = std::make_shared<std::atomic<bool>>(false);
	connect(progress, &QProgressDialog::canceled, this, [flag = cancel]() { *flag = true; });

	blog(LOG_INFO, "Downloading MetaOBS %s from %s", QT_TO_UTF8(release.version), QT_TO_UTF8(release.packageUrl));

	RunWorker([this, release = release, path = packagePath + ".part", flag = cancel]() {
		auto report = [this](int percent) {
			QMetaObject::invokeMethod(this, [this, percent]() { DownloadProgress(percent); },
						  Qt::QueuedConnection);
		};
		QString error = DownloadPackage(release, path, *flag, report);
		bool canceled = *flag;

		QMetaObject::invokeMethod(
			this, [this, canceled, error]() { DownloadFinished(canceled, error); }, Qt::QueuedConnection);
	});
}

void Updater::DownloadProgress(int percent)
{
	if (progress) {
		progress->setValue(percent);
	}
}

void Updater::DownloadFinished(bool canceled, const QString &error)
{
	JoinWorker();
	if (progress) {
		progress->close();
	}

	QString partPath = packagePath + ".part";
	if (canceled || !error.isEmpty()) {
		QFile::remove(partPath);
		if (canceled) {
			blog(LOG_INFO, "MetaOBS download canceled");
		} else {
			blog(LOG_WARNING, "MetaOBS download failed: %s", QT_TO_UTF8(error));
			OBSMessageBox::warning(DialogParent(), QTStr("MetaOBS.Updater.Title"),
					       QTStr("MetaOBS.Updater.DownloadFailed").arg(error));
		}
		Finish();
		return;
	}

	QFile::remove(packagePath);
	QString installError;
	if (!QFile::rename(partPath, packagePath)) {
		installError = QStringLiteral("could not rename %1").arg(NativePath(partPath));
	} else {
		OBSMessageBox::information(DialogParent(), QTStr("MetaOBS.Updater.Title"),
					   QTStr("MetaOBS.Updater.ReadyToInstall").arg(release.version));
		if (Install(installError)) {
			/* The install script waits for MetaOBS to close. The
			 * updater stays busy so it does not start again. */
			QMetaObject::invokeMethod(main, &OBSBasic::close, Qt::QueuedConnection);
			return;
		}
	}

	blog(LOG_WARNING, "MetaOBS install failed to start: %s", QT_TO_UTF8(installError));
	QFile::remove(partPath);
	QFile::remove(packagePath);
	OBSMessageBox::warning(DialogParent(), QTStr("MetaOBS.Updater.Title"),
			       QTStr("MetaOBS.Updater.StartFailed").arg(installError));
	Finish();
}

bool Updater::Install(QString &error)
{
	/* The program is in bin\64bit of the installation. */
	QString installDir = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
	if (!QFileInfo(installDir + "/bin").isDir() || !QFileInfo(installDir + "/data").isDir()) {
		error = QTStr("MetaOBS.Updater.UnknownLayout").arg(NativePath(installDir));
		return false;
	}

	std::string scriptSource;
	if (!GetDataFilePath(scriptName, scriptSource)) {
		error = QStringLiteral("%1 is missing").arg(scriptName);
		return false;
	}

	/* The script runs from the updates directory, since the installation
	 * it comes from is about to be replaced. */
	QString script = UpdatesPath(scriptName);
	QFile::remove(script);
	if (!QFile::copy(QT_UTF8(scriptSource.c_str()), script)) {
		error = QStringLiteral("could not copy %1").arg(scriptName);
		return false;
	}

	bool elevated = !CanWrite(installDir);

	QJsonObject config;
	config["version"] = release.version;
	config["packagePath"] = NativePath(packagePath);
	config["installDir"] = NativePath(installDir);
	config["executable"] = NativePath(QCoreApplication::applicationFilePath());
	config["arguments"] = RestartArguments();
	config["processId"] = static_cast<qint64>(GetCurrentProcessId());
	config["elevated"] = elevated;
	config["resultPath"] = NativePath(UpdatesPath(resultName));
	config["logPath"] = NativePath(UpdatesPath(logName));

	QString configPath = UpdatesPath(configName);
	QFile configFile(configPath);
	if (!configFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
	    configFile.write(QJsonDocument(config).toJson()) < 0) {
		error = configFile.errorString();
		return false;
	}
	configFile.close();
	QFile::remove(UpdatesPath(resultName));

	wchar_t systemDirectory[MAX_PATH] = {};
	GetSystemDirectoryW(systemDirectory, MAX_PATH);
	std::wstring powershell = std::wstring(systemDirectory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
	std::wstring parameters = QStringLiteral("-NoProfile -NonInteractive -ExecutionPolicy Bypass "
						 "-WindowStyle Hidden -File \"%1\" -ConfigPath \"%2\"")
					  .arg(NativePath(script), NativePath(configPath))
					  .toStdWString();

	/* Without write access to the installation, the script asks for
	 * administrator rights only to copy the files. */
	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC;
	info.lpFile = powershell.c_str();
	info.lpParameters = parameters.c_str();
	info.nShow = SW_HIDE;

	if (!ShellExecuteExW(&info)) {
		error = QStringLiteral("ShellExecuteEx failed (%1)").arg(GetLastError());
		return false;
	}

	blog(LOG_INFO, "Installing MetaOBS %s into %s%s", QT_TO_UTF8(release.version), QT_TO_UTF8(installDir),
	     elevated ? " with administrator rights" : "");

	/* Check again right after the update, in case of problems with the
	 * new version. */
	config_set_int(App()->GetAppConfig(), "General", "LastUpdateCheck", 0);
	config_set_string(App()->GetAppConfig(), "General", skipVersionKey, "");
	return true;
}

void Updater::ReportInstallResult()
{
	QFile file(UpdatesPath(resultName));
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	QByteArray data = file.readAll();
	file.close();
	file.remove();

	if (data.startsWith("\xEF\xBB\xBF")) {
		data.remove(0, 3);
	}
	QJsonObject result = QJsonDocument::fromJson(data).object();
	QString version = result["version"].toString();

	if (result["success"].toBool()) {
		blog(LOG_INFO, "MetaOBS %s was installed", QT_TO_UTF8(version));
		OBSMessageBox::information(DialogParent(), QTStr("MetaOBS.Updater.Title"),
					   QTStr("MetaOBS.Updater.Installed").arg(version));
	} else {
		QString error = result["error"].toString();
		blog(LOG_WARNING, "Installing MetaOBS %s failed: %s", QT_TO_UTF8(version), QT_TO_UTF8(error));
		OBSMessageBox::warning(
			DialogParent(), QTStr("MetaOBS.Updater.Title"),
			QTStr("MetaOBS.Updater.InstallFailed").arg(version, error, NativePath(UpdatesPath(logName))));
	}
}

} // namespace MetaOBS
