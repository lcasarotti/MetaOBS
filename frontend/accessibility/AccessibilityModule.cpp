#include "AccessibilityModule.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleMainWindow.hpp"
#include "AccessibleSettings.hpp"
#include "Announcer.hpp"
#include "DialogFixer.hpp"
#ifdef _WIN32
#include "ForeignWindowFocus.hpp"
#endif
#include "OutputAnnouncements.hpp"
#include "StatusMonitor.hpp"
#include "StatusReadouts.hpp"
#ifdef _WIN32
#include "FramingCheck.hpp"
#include "MidiDialog.hpp"
#include "MidiLearn.hpp"
#include "MidiManager.hpp"
#include "StreamDeckDialog.hpp"
#include "StreamDeckManager.hpp"
#endif

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <qt-wrappers.hpp>
#include <util/text-lookup.h>

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QMenu>
#include <QPointer>
#include <QUrl>

#include <string>

namespace OBS::Accessibility {

namespace {
constexpr const char *defaultLocale = "en-US";

StartupMode startupMode = StartupMode::FromSettings;
OBSBasic *mainWindow = nullptr;
QPointer<AccessibleMainWindow> accessibleWindow;
bool accessibleModeActive = false;
bool startInTray = false;

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

/* A checkable item in the tray icon's menu, so the mode can be changed while
 * OBS is hidden in the tray. */
void AddModeActionToTray(OBSBasic *main)
{
	QMenu *trayMenu = OBSAccessibleBridge::TrayMenu(main);
	if (!trayMenu) {
		return;
	}

	QAction *action = new QAction(QTStr("Accessibility.Action.AccessibleMode"), trayMenu);
	action->setCheckable(true);
	QObject::connect(action, &QAction::triggered, main, [](bool checked) {
		if (checked) {
			SwitchToAccessibleMode();
		} else {
			SwitchToStandardMode();
		}
	});
	QObject::connect(trayMenu, &QMenu::aboutToShow, action,
			 [action]() { action->setChecked(IsAccessibleModeActive()); });

	QList<QAction *> actions = trayMenu->actions();
	trayMenu->insertAction(actions.size() > 1 ? actions[1] : nullptr, action);
}

/* The manual is installed for some languages only; the others get the
 * English one. */
void OpenManual()
{
	std::string path;
	std::string relativePath = std::string("manual/") + App()->GetLocale() + ".html";
	if (!GetDataFilePath(relativePath.c_str(), path) && !GetDataFilePath("manual/en-US.html", path)) {
		blog(LOG_WARNING, "[Accessibility] Manual not found");
		OBSMessageBox::warning(QApplication::activeWindow(), QTStr("Accessibility.Manual.Title"),
				       QTStr("Accessibility.Manual.NotFound"));
		return;
	}

	QDesktopServices::openUrl(QUrl::fromLocalFile(QT_UTF8(path.c_str())));
}

/* The Help menu is shared by both windows. F1 works in every window of the
 * application, since the main window is hidden in Accessible Mode. */
void AddManualAction(OBSBasic *main)
{
	QAction *action = new QAction(QTStr("Accessibility.Manual.Action"), main);
	action->setShortcut(QKeySequence(Qt::Key_F1));
	action->setShortcutContext(Qt::ApplicationShortcut);
	QObject::connect(action, &QAction::triggered, main, []() { OpenManual(); });

	QMenu *helpMenu = OBSAccessibleBridge::HelpMenu(main);
	QAction *first = helpMenu->actions().value(0);
	helpMenu->insertAction(first, action);
	helpMenu->insertSeparator(first);
}

#ifdef _WIN32
/* The Tools menu is shared by both windows. */
void AddStreamDeckAction(OBSBasic *main)
{
	new StreamDeckManager(main);

	auto action = static_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction(QT_TO_UTF8(QTStr("Accessibility.StreamDeck.MenuItem"))));
	QObject::connect(action, &QAction::triggered, main, [main]() {
		QWidget *parent = QApplication::activeWindow();
		StreamDeckDialog::Open(parent ? parent : main);
	});
}

void AddMidiAction(OBSBasic *main)
{
	MidiManager *manager = new MidiManager(main);
	qApp->installEventFilter(new MidiLearn(manager, main));

	auto action = static_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction(QT_TO_UTF8(QTStr("Accessibility.Midi.MenuItem"))));
	QObject::connect(action, &QAction::triggered, main, [main]() {
		QWidget *parent = QApplication::activeWindow();
		MidiDialog::Open(parent ? parent : main);
	});
}
#endif
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

	/* The main window asks this before Initialize runs, so the defaults
	 * have to be in place here already. */
	Settings::SetDefaults();
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

	/* These objects are owned by the main window, so they are destroyed
	 * together with it and before OBS shuts down. */
	new Announcer(main);

	OutputAnnouncements *outputAnnouncements = new OutputAnnouncements(main);
	outputAnnouncements->ConnectStatusBar(main->statusBar());

	new StatusMonitor(main);
	new StatusHotkeys(main);

	qApp->installEventFilter(new DialogFixer(main));
#ifdef _WIN32
	qApp->installEventFilter(new ForeignWindowFocus(main));
	AddStreamDeckAction(main);
	AddMidiAction(main);
	new FramingCheck(main);
#endif

	AddModeActionToMainWindow(main);
	AddManualAction(main);

	AddModeActionToTray(main);

	if (ShouldStartInAccessibleMode()) {
		SwitchToAccessibleMode(!startInTray);
	}
}

bool IsAccessibleModeActive()
{
	return accessibleModeActive;
}

void SwitchToAccessibleMode(bool show)
{
	if (!mainWindow) {
		return;
	}

	/* Hides the main window and its dialogs, and stops drawing its
	 * preview. Does nothing if it is already hidden. This comes first,
	 * since requests to hide go to the accessible window once the mode is
	 * active. */
	OBSAccessibleBridge::SetMainWindowShowing(mainWindow, false);

	accessibleModeActive = true;

	if (!accessibleWindow) {
		accessibleWindow = new AccessibleMainWindow(mainWindow);
	}

	if (show) {
		accessibleWindow->ShowAndActivate();
	}
	OBSAccessibleBridge::UpdateTrayShowHide(mainWindow, show);
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

bool RedirectShowingRequest(bool showing)
{
	if (!accessibleModeActive || !accessibleWindow) {
		return false;
	}

	if (showing) {
		accessibleWindow->ShowAndActivate();
	} else {
		accessibleWindow->hide();
	}
	OBSAccessibleBridge::UpdateTrayShowHide(mainWindow, showing);
	return true;
}

bool IsAccessibleWindowShowing()
{
	return accessibleModeActive && accessibleWindow && accessibleWindow->isVisible();
}

void SetStartInTray(bool startInTray_)
{
	startInTray = startInTray_;
}

} // namespace OBS::Accessibility
