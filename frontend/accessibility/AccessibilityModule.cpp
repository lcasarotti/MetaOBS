#include "AccessibilityModule.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleMainWindow.hpp"
#include "AccessibleSettings.hpp"
#include "Announcer.hpp"
#include "DialogFixer.hpp"
#include "OutputAnnouncements.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <util/text-lookup.h>

#include <QAction>
#include <QMenu>
#include <QPointer>

#include <string>

namespace OBS::Accessibility {

namespace {
constexpr const char *defaultLocale = "en-US";

StartupMode startupMode = StartupMode::FromSettings;
OBSBasic *mainWindow = nullptr;
QPointer<AccessibleMainWindow> accessibleWindow;
bool accessibleModeActive = false;

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

void AddModeActionToMainWindow(OBSBasic *main)
{
	QAction *action = new QAction(QTStr("Accessibility.Action.AccessibleMode"), main);
	action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
	QObject::connect(action, &QAction::triggered, main, []() { SwitchToAccessibleMode(); });

	QMenu *viewMenu = OBSAccessibleBridge::ViewMenu(main);
	viewMenu->addSeparator();
	viewMenu->addAction(action);
}
} // namespace

void SetStartupMode(StartupMode mode)
{
	startupMode = mode;
}

bool ShouldStartInAccessibleMode()
{
	switch (startupMode) {
	case StartupMode::Accessible:
		return true;
	case StartupMode::Standard:
		return false;
	case StartupMode::FromSettings:
		break;
	}

	return Settings::GetAccessibleMode();
}

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
	mainWindow = main;
	Settings::SetDefaults();

	/* Both objects are owned by the main window, so they are destroyed
	 * together with it and before OBS shuts down. */
	new Announcer(main);

	OutputAnnouncements *outputAnnouncements = new OutputAnnouncements(main);
	outputAnnouncements->ConnectStatusBar(main->statusBar());

	qApp->installEventFilter(new DialogFixer(main));

	AddModeActionToMainWindow(main);

	if (ShouldStartInAccessibleMode()) {
		SwitchToAccessibleMode();
	}
}

bool IsAccessibleModeActive()
{
	return accessibleModeActive;
}

void SwitchToAccessibleMode()
{
	if (!mainWindow) {
		return;
	}

	accessibleModeActive = true;

	if (!accessibleWindow) {
		accessibleWindow = new AccessibleMainWindow(mainWindow);
	}

	/* Hides the main window and its dialogs, and stops drawing its
	 * preview. Does nothing if it is already hidden. */
	OBSAccessibleBridge::SetMainWindowShowing(mainWindow, false);

	accessibleWindow->ShowAndActivate();
	blog(LOG_INFO, "[Accessibility] Switched to Accessible Mode");
}

void SwitchToStandardMode()
{
	if (!mainWindow) {
		return;
	}

	accessibleModeActive = false;

	if (accessibleWindow) {
		accessibleWindow->hide();
	}

	OBSAccessibleBridge::SetMainWindowShowing(mainWindow, true);
	mainWindow->activateWindow();
	blog(LOG_INFO, "[Accessibility] Switched to the standard interface");
}

bool RedirectShowRequest()
{
	if (!accessibleModeActive || !accessibleWindow) {
		return false;
	}

	accessibleWindow->ShowAndActivate();
	return true;
}

} // namespace OBS::Accessibility
