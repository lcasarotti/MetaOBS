#include "AccessibleMainWindow.hpp"
#include "AccessibilityModule.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleCommon.hpp"
#include "AudioPanel.hpp"
#include "CommandPalette.hpp"
#include "ControlsPanel.hpp"
#include "ScenesPanel.hpp"
#include "SourcesPanel.hpp"
#include "StatusPanel.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include "moc_AccessibleMainWindow.cpp"

namespace OBS::Accessibility {

namespace {
/* The File, Profile, Scene Collection, Tools and Help menus are shared with
 * the main window's menu bar. Qt caches the accessible interface of each menu
 * bar item with the first menu bar that showed it, and an item whose menu bar
 * is hidden is invisible to screen readers. Dropping the cached interfaces
 * lets Qt create them again for the visible menu bar. */
void ResetMenuBarAccessibility(QMenuBar *bar)
{
	for (QAction *action : bar->actions()) {
		if (QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(action)) {
			QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(iface));
		}
	}
}
} // namespace

AccessibleMainWindow::AccessibleMainWindow(OBSBasic *main_) : QMainWindow(nullptr), main(main_)
{
	setObjectName("AccessibleMainWindow");

	QWidget *central = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(central);
	QHBoxLayout *listsLayout = new QHBoxLayout();

	QHBoxLayout *audioLayout = new QHBoxLayout();

	scenesPanel = new ScenesPanel(main, central);
	sourcesPanel = new SourcesPanel(main, central);
	audioPanel = new AudioPanel(main, central);
	controlsPanel = new ControlsPanel(main, central);
	statusPanel = new StatusPanel(central);

	listsLayout->addWidget(scenesPanel, 1);
	listsLayout->addWidget(sourcesPanel, 2);
	audioLayout->addWidget(audioPanel, 2);
	audioLayout->addWidget(statusPanel, 1);
	layout->addLayout(listsLayout, 2);
	layout->addLayout(audioLayout, 1);
	layout->addWidget(controlsPanel);
	setCentralWidget(central);

	panels = {scenesPanel, sourcesPanel, audioPanel, controlsPanel, statusPanel};
	panelWidgets = {scenesPanel->View(), sourcesPanel->View(), audioPanel->View(), controlsPanel->FirstControl(),
			statusPanel->View()};

	for (int i = 1; i < panelWidgets.size(); i++) {
		setTabOrder(panelWidgets[i - 1], panelWidgets[i]);
	}

	/* F6 moves between panels, as in many Windows applications, and
	 * Ctrl+1 to Ctrl+5 jump to a panel. */
	QAction *nextPanel = new QAction(this);
	nextPanel->setShortcut(QKeySequence(Qt::Key_F6));
	connect(nextPanel, &QAction::triggered, this, [this]() { CyclePanels(1); });
	addAction(nextPanel);

	QAction *previousPanel = new QAction(this);
	previousPanel->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
	connect(previousPanel, &QAction::triggered, this, [this]() { CyclePanels(-1); });
	addAction(previousPanel);

	for (int i = 0; i < panelWidgets.size(); i++) {
		QAction *jump = new QAction(this);
		jump->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)));
		connect(jump, &QAction::triggered, this, [this, i]() { FocusPanel(i); });
		addAction(jump);
	}

	CreateMenus();

	connect(main, &QWidget::windowTitleChanged, this, &AccessibleMainWindow::UpdateTitle);
	UpdateTitle(main->windowTitle());

	/* Let OBS hotkeys work while this window has focus, as the main window
	 * does. */
	installEventFilter(CreateShortcutFilter());

	qApp->installEventFilter(this);
	obs_frontend_add_event_callback(OnFrontendEvent, this);

	resize(1100, 800);
}

AccessibleMainWindow::~AccessibleMainWindow()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
}

void AccessibleMainWindow::CreateMenus()
{
	QMenuBar *bar = menuBar();

	bar->addMenu(OBSAccessibleBridge::FileMenu(main));

	QMenu *editMenu = bar->addMenu(QTStr("Basic.MainMenu.Edit"));
	editMenu->addAction(CreateMirrorAction(OBSAccessibleBridge::UndoAction(main), editMenu));
	editMenu->addAction(CreateMirrorAction(OBSAccessibleBridge::RedoAction(main), editMenu));

	QMenu *viewMenu = bar->addMenu(QTStr("Basic.MainMenu.View"));
	QAction *standardInterface = viewMenu->addAction(QTStr("Accessibility.Action.StandardInterface"));
	standardInterface->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
	connect(standardInterface, &QAction::triggered, this, []() { SwitchToStandardMode(); });
	QAction *commandPalette = viewMenu->addAction(QTStr("Accessibility.Palette.Action"));
	commandPalette->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
	connect(commandPalette, &QAction::triggered, this, [this]() { CommandPalette::Run(menuBar(), this); });
	CommandPalette::Exclude(commandPalette);
	viewMenu->addSeparator();
	AddProjectorMenus(viewMenu);

	/* Shortcuts of the window and its panels, which mirrored main window
	 * actions must not take. */
	QList<QKeySequence> reserved;
	QList<QWidget *> shortcutOwners = {this, scenesPanel->View(), sourcesPanel->View(), audioPanel->View()};
	for (QWidget *widget : shortcutOwners) {
		for (QAction *action : widget->actions()) {
			reserved.append(action->shortcuts());
		}
	}
	reserved.append(standardInterface->shortcut());
	reserved.append(commandPalette->shortcut());
	sourcesPanel->AddMirroredActions(reserved);

	bar->addMenu(scenesPanel->Menu());
	bar->addMenu(sourcesPanel->Menu());
	bar->addMenu(audioPanel->Menu());
	controlsPanel->FillMenu(bar->addMenu(QTStr("Accessibility.Menu.Controls")));
	bar->addMenu(statusPanel->Menu());

	bar->addMenu(OBSAccessibleBridge::ProfileMenu(main));
	bar->addMenu(OBSAccessibleBridge::SceneCollectionMenu(main));
	bar->addMenu(OBSAccessibleBridge::ToolsMenu(main));
	bar->addMenu(OBSAccessibleBridge::HelpMenu(main));
}

void AccessibleMainWindow::AddProjectorMenus(QMenu *viewMenu)
{
	/* In Studio Mode the preview and program have separate projectors. */
	QMenu *previewMenu = viewMenu->addMenu(QTStr("Projector.Open.Preview"));
	connect(previewMenu, &QMenu::aboutToShow, this, [previewMenu]() { FillProjectorMenu(previewMenu, "Preview"); });

	QMenu *programMenu = viewMenu->addMenu(QTStr("Projector.Open.Program"));
	connect(programMenu, &QMenu::aboutToShow, this,
		[programMenu]() { FillProjectorMenu(programMenu, "StudioProgram"); });

	QMenu *multiviewMenu = viewMenu->addMenu(QTStr("Projector.Open.Multiview"));
	connect(multiviewMenu, &QMenu::aboutToShow, this,
		[multiviewMenu]() { FillProjectorMenu(multiviewMenu, "Multiview"); });

	connect(viewMenu, &QMenu::aboutToShow, this, [programMenu]() {
		programMenu->menuAction()->setVisible(obs_frontend_preview_program_mode_active());
	});

	viewMenu->addSeparator();
	QAction *screenshot = viewMenu->addAction(QTStr("Screenshot"));
	connect(screenshot, &QAction::triggered, this, [this]() { OBSAccessibleBridge::ScreenshotProgram(main); });
}

void AccessibleMainWindow::ShowAndActivate()
{
	ResetMenuBarAccessibility(menuBar());
	if (isMinimized()) {
		setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	}
	show();
	raise();
	activateWindow();

	if (!isAncestorOf(QApplication::focusWidget())) {
		FocusPanel(0);
	}
}

void AccessibleMainWindow::FocusPanel(int index)
{
	if (index < 0 || index >= panelWidgets.size()) {
		return;
	}

	QWidget *widget = panelWidgets[index];
	if (widget && widget->isVisible()) {
		widget->setFocus(Qt::ShortcutFocusReason);
	}
}

void AccessibleMainWindow::CyclePanels(int direction)
{
	QWidget *focused = QApplication::focusWidget();

	int current = -1;
	for (int i = 0; i < panels.size(); i++) {
		if (focused && (panels[i] == focused || panels[i]->isAncestorOf(focused))) {
			current = i;
			break;
		}
	}

	int count = static_cast<int>(panels.size());
	int next = current < 0 ? 0 : (current + direction + count) % count;
	FocusPanel(next);
}

void AccessibleMainWindow::UpdateTitle(const QString &mainTitle)
{
	setWindowTitle(QTStr("Accessibility.Window.Title").arg(mainTitle));
}

void AccessibleMainWindow::closeEvent(QCloseEvent *event)
{
	/* Closing this window exits OBS, with the main window's checks and
	 * confirmations. */
	event->ignore();
	QTimer::singleShot(0, main, [main = main]() { main->close(); });
}

void AccessibleMainWindow::hideEvent(QHideEvent *event)
{
	if (!event->spontaneous()) {
		ResetMenuBarAccessibility(main->menuBar());
	}
	QMainWindow::hideEvent(event);
}

/* Minimizing hides the window in the tray when the settings ask for it, as
 * with the main window. */
void AccessibleMainWindow::changeEvent(QEvent *event)
{
	bool minimized = event->type() == QEvent::WindowStateChange && isMinimized();
	if (minimized && OBSAccessibleBridge::MinimizesToTray(main)) {
		QTimer::singleShot(0, main, [main = main]() { OBSAccessibleBridge::SetMainWindowShowing(main, false); });
	}
	QMainWindow::changeEvent(event);
}

/* Dialogs are owned by the hidden main window, so when one closes the
 * window system may activate nothing, or another application, instead of
 * this window. */
bool AccessibleMainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::Hide && watched->isWidgetType() && watched != this) {
		QWidget *widget = static_cast<QWidget *>(watched);
		Qt::WindowType type = widget->windowType();

		if (widget->isWindow() && !qobject_cast<QMenu *>(widget) && type != Qt::ToolTip &&
		    type != Qt::Popup) {
			QTimer::singleShot(0, this, &AccessibleMainWindow::ReturnFocusAfterDialog);
		}
	}

	return QMainWindow::eventFilter(watched, event);
}

void AccessibleMainWindow::ReturnFocusAfterDialog()
{
	if (!isVisible() || QApplication::activeModalWidget()) {
		return;
	}

	QWidget *active = QApplication::activeWindow();
	if (active && active != this && active != main && active->isVisible()) {
		return;
	}

	raise();
	activateWindow();
}

void AccessibleMainWindow::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		auto window = static_cast<AccessibleMainWindow *>(data);
		window->hide();
		window->deleteLater();
	}
}

} // namespace OBS::Accessibility
