#pragma once

class OBSBasic;

/* Entry points of the accessibility module. Kept to a handful of calls so the
 * hooks in upstream files stay small. */
namespace OBS::Accessibility {

enum class StartupMode {
	FromSettings,
	Accessible,
	Standard,
};

/* Set from the command line (--accessible-mode, --standard-mode) before the
 * main window is created. */
void SetStartupMode(StartupMode mode);

/* Whether OBS starts with the accessible window instead of the main window. */
bool ShouldStartInAccessibleMode();

/* Adds the accessibility translations to the application's text lookup.
 * Call right after the main locale has been loaded. */
void LoadLocale();

/* Sets up announcements and Accessible Mode. Call once the main window has
 * been initialized. */
void Initialize(OBSBasic *main);

bool IsAccessibleModeActive();
void SwitchToAccessibleMode();
void SwitchToStandardMode();

/* Called when something asks the main window to show itself (tray icon,
 * exit confirmation). In Accessible Mode, shows the accessible window instead
 * and returns true. */
bool RedirectShowRequest();

} // namespace OBS::Accessibility
