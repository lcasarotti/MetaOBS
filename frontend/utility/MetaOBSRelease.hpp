#pragma once

#include <QByteArray>
#include <QString>

/* Reading MetaOBS releases from the GitHub API. Nothing here depends on the
 * running application, so the unit tests can cover it. */
namespace MetaOBS {

/* Release tags are "metaobs-v" followed by the version, e.g. metaobs-v1.2.0. */
constexpr const char *releaseTagPrefix = "metaobs-v";

/* The package of a release that the Windows updater installs. */
constexpr const char *windowsPackageSuffix = "-windows-x64.zip";

struct Release {
	QString version;
	QString name;
	QString notes;
	QString pageUrl;

	/* Empty when the release has no package for this platform. */
	QString packageUrl;
	qint64 packageSize = 0;
	/* The SHA-256 digest that GitHub computed for the package, or empty
	 * when the API did not report one. */
	QByteArray packageSha256;
};

/* True for dotted numeric versions such as "1.2" or "1.2.0". */
bool IsValidVersion(const QString &version);

/* Compares two valid versions part by part, with missing parts counting as
 * zero. Returns a negative number, zero or a positive number. */
int CompareVersions(const QString &a, const QString &b);

/* Parses a release object from the GitHub REST API, picking the asset whose
 * name ends with packageSuffix. */
bool ParseRelease(const QByteArray &json, const QString &packageSuffix, Release &release, QString &error);

} // namespace MetaOBS
