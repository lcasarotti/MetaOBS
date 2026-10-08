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
/* Shows the accessible window in place of the main window, or with show set
 * to false, keeps both hidden in the tray. */
void SwitchToAccessibleMode(bool show = true);
void SwitchToStandardMode();

/* Called when something asks the main window to show or hide itself (tray
 * icon, minimizing to the tray, exit confirmation). In Accessible Mode, shows
 * or hides the accessible window instead and returns true. */
bool RedirectShowingRequest(bool showing);

/* Whether the accessible window is on screen, for the tray icon's Show and
 * Hide. */
bool IsAccessibleWindowShowing();

/* Set by the main window before it is shown, when OBS starts in the tray, so
 * that Accessible Mode starts there too. */
void SetStartInTray(bool startInTray);

} // namespace OBS::Accessibility
