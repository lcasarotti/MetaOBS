#include "MetaOBSRelease.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cstring>

namespace MetaOBS {

bool IsValidVersion(const QString &version)
{
	static const QRegularExpression pattern(R"(^\d{1,9}(\.\d{1,9}){0,3}$)");
	return pattern.match(version).hasMatch();
}

int CompareVersions(const QString &a, const QString &b)
{
	QStringList partsA = a.split('.');
	QStringList partsB = b.split('.');
	qsizetype count = std::max(partsA.size(), partsB.size());

	for (qsizetype i = 0; i < count; i++) {
		int partA = i < partsA.size() ? partsA[i].toInt() : 0;
		int partB = i < partsB.size() ? partsB[i].toInt() : 0;
		if (partA != partB) {
			return partA < partB ? -1 : 1;
		}
	}
	return 0;
}

bool ParseRelease(const QByteArray &json, const QString &packageSuffix, Release &release, QString &error)
{
	QJsonParseError parseError;
	QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (!document.isObject()) {
		error = parseError.error != QJsonParseError::NoError ? parseError.errorString()
								     : QStringLiteral("not a release object");
		return false;
	}

	QJsonObject object = document.object();
	QString tag = object["tag_name"].toString();
	if (!tag.startsWith(releaseTagPrefix)) {
		error = QStringLiteral("unexpected tag \"%1\"").arg(tag);
		return false;
	}

	release = Release();
	release.version = tag.mid(qsizetype(strlen(releaseTagPrefix)));
	if (!IsValidVersion(release.version)) {
		error = QStringLiteral("invalid version in tag \"%1\"").arg(tag);
		return false;
	}

	release.name = object["name"].toString();
	if (release.name.isEmpty()) {
		release.name = QStringLiteral("MetaOBS %1").arg(release.version);
	}
	release.notes = object["body"].toString();
	release.pageUrl = object["html_url"].toString();

	for (const QJsonValue &value : object["assets"].toArray()) {
		QJsonObject asset = value.toObject();
		if (!asset["name"].toString().endsWith(packageSuffix, Qt::CaseInsensitive)) {
			continue;
		}

		release.packageUrl = asset["browser_download_url"].toString();
		release.packageSize = asset["size"].toInteger();

		QString digest = asset["digest"].toString();
		if (digest.startsWith("sha256:")) {
			release.packageSha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
		}
		break;
	}

	return true;
}

} // namespace MetaOBS
