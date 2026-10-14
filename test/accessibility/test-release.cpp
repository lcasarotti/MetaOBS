/* Checks how MetaOBS reads its releases from the GitHub API and compares
 * versions, which decides whether an update is offered. */

#include "a11y-test.hpp"

#include <utility/MetaOBSRelease.hpp>

using MetaOBS::CompareVersions;
using MetaOBS::IsValidVersion;
using MetaOBS::ParseRelease;
using MetaOBS::Release;
using MetaOBS::windowsInstallerSuffix;
using MetaOBS::windowsPackageSuffix;

namespace {
const char *releaseJson = R"({
	"tag_name": "metaobs-v1.2.0",
	"name": "MetaOBS 1.2.0",
	"body": "- Fixed things",
	"html_url": "https://github.com/lcasarotti/MetaOBS/releases/tag/metaobs-v1.2.0",
	"assets": [
		{
			"name": "MetaOBS-1.2.0-macos.dmg",
			"browser_download_url": "https://github.com/lcasarotti/MetaOBS/releases/download/metaobs-v1.2.0/MetaOBS-1.2.0-macos.dmg",
			"size": 10
		},
		{
			"name": "MetaOBS-1.2.0-windows-x64-installer.exe",
			"browser_download_url": "https://github.com/lcasarotti/MetaOBS/releases/download/metaobs-v1.2.0/MetaOBS-1.2.0-windows-x64-installer.exe",
			"size": 654321,
			"digest": "sha256:ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"
		},
		{
			"name": "MetaOBS-1.2.0-windows-x64.zip",
			"browser_download_url": "https://github.com/lcasarotti/MetaOBS/releases/download/metaobs-v1.2.0/MetaOBS-1.2.0-windows-x64.zip",
			"size": 123456,
			"digest": "sha256:00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
		}
	]
})";
} // namespace

A11Y_TEST(ReleaseVersionsCompareByNumber)
{
	A11Y_CHECK(CompareVersions("1.10.0", "1.9.0") > 0);
	A11Y_CHECK(CompareVersions("1.2", "1.2.0") == 0);
	A11Y_CHECK(CompareVersions("1.2.0", "1.2.1") < 0);
	A11Y_CHECK(CompareVersions("2.0.0", "1.99.99") > 0);
}

A11Y_TEST(ReleaseVersionsAreDottedNumbers)
{
	A11Y_CHECK(IsValidVersion("1"));
	A11Y_CHECK(IsValidVersion("1.2.0"));
	A11Y_CHECK(!IsValidVersion(""));
	A11Y_CHECK(!IsValidVersion("1.2.0-beta1"));
	A11Y_CHECK(!IsValidVersion("v1.2"));
	A11Y_CHECK(!IsValidVersion("1..2"));
}

A11Y_TEST(ReleasePicksTheWindowsPackage)
{
	Release release;
	QString error;
	A11Y_CHECK(ParseRelease(releaseJson, windowsPackageSuffix, release, error));
	A11Y_CHECK(release.version == "1.2.0");
	A11Y_CHECK(release.name == "MetaOBS 1.2.0");
	A11Y_CHECK(release.notes == "- Fixed things");
	A11Y_CHECK(release.packageName == "MetaOBS-1.2.0-windows-x64.zip");
	A11Y_CHECK(release.packageUrl.endsWith("MetaOBS-1.2.0-windows-x64.zip"));
	A11Y_CHECK(release.packageSize == 123456);
	A11Y_CHECK(release.packageSha256.size() == 32);
	A11Y_CHECK(release.packageSha256.toHex() ==
		   "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
}

A11Y_TEST(ReleasePicksTheInstaller)
{
	Release release;
	QString error;
	A11Y_CHECK(ParseRelease(releaseJson, windowsInstallerSuffix, release, error));
	A11Y_CHECK(release.packageName == "MetaOBS-1.2.0-windows-x64-installer.exe");
	A11Y_CHECK(release.packageSize == 654321);
	A11Y_CHECK(release.packageSha256.toHex() ==
		   "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100");
}

A11Y_TEST(ReleaseWithoutPackageHasNoUrl)
{
	Release release;
	QString error;
	A11Y_CHECK(ParseRelease(R"({"tag_name": "metaobs-v1.0.0", "assets": []})", windowsPackageSuffix, release,
				error));
	A11Y_CHECK(release.packageUrl.isEmpty());
	A11Y_CHECK(release.packageSha256.isEmpty());
	A11Y_CHECK(release.name == "MetaOBS 1.0.0");
}

A11Y_TEST(ReleaseRejectsOtherTags)
{
	Release release;
	QString error;
	A11Y_CHECK(!ParseRelease(R"({"tag_name": "32.0.0"})", windowsPackageSuffix, release, error));
	A11Y_CHECK(!error.isEmpty());
	A11Y_CHECK(!ParseRelease(R"({"tag_name": "metaobs-vnext"})", windowsPackageSuffix, release, error));
	A11Y_CHECK(!ParseRelease("not json", windowsPackageSuffix, release, error));
}
