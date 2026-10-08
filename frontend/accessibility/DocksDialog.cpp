#include "DocksDialog.hpp"
#include "AccessibleBridge.hpp"

#include <OBSApp.hpp>

#include <QDialogButtonBox>
#include <QDockWidget>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "moc_DocksDialog.cpp"

namespace OBS::Accessibility {

DocksDialog::DocksDialog(OBSBasic *main_, QWidget *parent) : QDialog(parent), main(main_)
{
	setWindowTitle(QTStr("Accessibility.Docks.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	QLabel *label = new QLabel(QTStr("Accessibility.Docks.List"), this);
	list = new QListWidget(this);
	label->setBuddy(list);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	openButton = buttons->addButton(QTStr("Accessibility.Docks.Open"), QDialogButtonBox::ActionRole);
	hideButton = buttons->addButton(QTStr("Accessibility.Docks.Hide"), QDialogButtonBox::ActionRole);
	if (OBSAccessibleBridge::HasBrowserDocks()) {
		QPushButton *browserDocks =
			buttons->addButton(QTStr("Accessibility.Docks.BrowserDocks"), QDialogButtonBox::ActionRole);
		connect(browserDocks, &QPushButton::clicked, this, [this]() {
			accept();
			OBSAccessibleBridge::ManageBrowserDocks(main);
		});
	}

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(label);
	layout->addWidget(list);
	layout->addWidget(buttons);

	/* Enter on a dock opens it rather than pressing a button. */
	for (QPushButton *button : buttons->findChildren<QPushButton *>()) {
		button->setAutoDefault(false);
	}

	connect(list, &QListWidget::itemActivated, this, &DocksDialog::OpenSelected);
	connect(list, &QListWidget::currentRowChanged, this, [this]() {
		QDockWidget *dock = SelectedDock();
		openButton->setEnabled(dock != nullptr);
		hideButton->setEnabled(dock && dock->isVisible());
	});
	connect(openButton, &QPushButton::clicked, this, &DocksDialog::OpenSelected);
	connect(hideButton, &QPushButton::clicked, this, &DocksDialog::HideSelected);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	Refresh();
	list->setFocus();
	resize(450, 350);
}

void DocksDialog::Open(OBSBasic *main, QWidget *parent)
{
	DocksDialog *dialog = new DocksDialog(main, parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->open();
}

void DocksDialog::Refresh()
{
	int row = std::max(list->currentRow(), 0);

	list->clear();
	docks.clear();
	for (QDockWidget *dock : OBSAccessibleBridge::PluginDocks(main)) {
		QString title = dock->windowTitle();
		if (dock->isVisible()) {
			title = QTStr("Accessibility.Docks.Shown").arg(title);
		}
		list->addItem(title);
		docks.append(dock);
	}

	if (docks.isEmpty()) {
		list->addItem(QTStr("Accessibility.Docks.None"));
	}
	list->setCurrentRow(std::min(row, list->count() - 1));
}

QDockWidget *DocksDialog::SelectedDock() const
{
	int row = list->currentRow();
	return row >= 0 && row < docks.size() ? docks[row].data() : nullptr;
}

/* Docks normally sit in the main window, which is hidden, so they open as
 * floating windows of their own. */
void DocksDialog::OpenSelected()
{
	QPointer<QDockWidget> dock = SelectedDock();
	if (!dock) {
		return;
	}

	accept();

	dock->setFloating(true);
	dock->show();
	dock->raise();
	dock->activateWindow();

	QTimer::singleShot(0, dock, [dock]() {
		if (QWidget *content = dock->widget()) {
			content->setFocus(Qt::ActiveWindowFocusReason);
		}
	});
}

void DocksDialog::HideSelected()
{
	if (QDockWidget *dock = SelectedDock()) {
		dock->hide();
		Refresh();
	}
}

} // namespace OBS::Accessibility
