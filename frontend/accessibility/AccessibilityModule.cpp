#include "AccessibilityModule.hpp"
#include "AccessibleSettings.hpp"
#include "Announcer.hpp"
#include "OutputAnnouncements.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <util/text-lookup.h>

#include <string>

namespace OBS::Accessibility {

namespace {
constexpr const char *defaultLocale = "en-US";

void AddLocaleFile(const std::string &locale)
{
	std::string relativePath = "locale/accessibility/" + locale + ".ini";
	std::string path;

	if (!GetDataFilePath(relativePath.c_str(), path)) {
		return;
	}

	if (!text_lookup_add(App()->GetTextLookup(), path.c_str())) {
		blog(LOG_WARNING, "[Accessibility] Failed to add locale file '%s'", path.c_str());
	}
}
} // namespace

void LoadLocale()
{
	AddLocaleFile(defaultLocale);

	std::string locale = App()->GetLocale();
	if (locale != defaultLocale) {
		AddLocaleFile(locale);
	}
}

void Initialize(OBSBasic *main)
{
	Settings::SetDefaults();

	/* Both objects are owned by the main window, so they are destroyed
	 * together with it and before OBS shuts down. */
	new Announcer(main);

	OutputAnnouncements *outputAnnouncements = new OutputAnnouncements(main);
	outputAnnouncements->ConnectStatusBar(main->statusBar());
}

} // namespace OBS::Accessibility
