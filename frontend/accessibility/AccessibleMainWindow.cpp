#include "AccessibleMainWindow.hpp"
#include "AccessibilityModule.hpp"
#include "AccessibleBridge.hpp"
#include "ControlsPanel.hpp"
#include "ScenesPanel.hpp"
#include "SourcesPanel.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QListView>
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

	scenesPanel = new ScenesPanel(main, central);
	sourcesPanel = new SourcesPanel(main, central);
	controlsPanel = new ControlsPanel(main, central);

	listsLayout->addWidget(scenesPanel, 1);
	listsLayout->addWidget(sourcesPanel, 2);
	layout->addLayout(listsLayout, 1);
	layout->addWidget(controlsPanel);
	setCentralWidget(central);

	panelWidgets = {scenesPanel->View(), sourcesPanel->View(), controlsPanel->FirstControl()};

	setTabOrder(scenesPanel->View(), sourcesPanel->View());
	setTabOrder(sourcesPanel->View(), controlsPanel->FirstControl());

	/* F6 moves between panels, as in many Windows applications, and
	 * Ctrl+1 to Ctrl+3 jump to a panel. */
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

	resize(1000, 650);
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
	editMenu->addAction(OBSAccessibleBridge::UndoAction(main));
	editMenu->addAction(OBSAccessibleBridge::RedoAction(main));

	QMenu *viewMenu = bar->addMenu(QTStr("Basic.MainMenu.View"));
	QAction *standardInterface = viewMenu->addAction(QTStr("Accessibility.Action.StandardInterface"));
	standardInterface->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
	connect(standardInterface, &QAction::triggered, this, []() { SwitchToStandardMode(); });

	bar->addMenu(scenesPanel->Menu());
	bar->addMenu(sourcesPanel->Menu());
	controlsPanel->FillMenu(bar->addMenu(QTStr("Accessibility.Menu.Controls")));

	bar->addMenu(OBSAccessibleBridge::ProfileMenu(main));
	bar->addMenu(OBSAccessibleBridge::SceneCollectionMenu(main));
	bar->addMenu(OBSAccessibleBridge::ToolsMenu(main));
	bar->addMenu(OBSAccessibleBridge::HelpMenu(main));
}

void AccessibleMainWindow::ShowAndActivate()
{
	ResetMenuBarAccessibility(menuBar());
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
	QList<QWidget *> panels = {scenesPanel, sourcesPanel, controlsPanel};

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
