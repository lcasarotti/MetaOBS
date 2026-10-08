#include "StatusPanel.hpp"
#include "AccessibleCommon.hpp"
#include "StatusMonitor.hpp"
#include "StatusReadouts.hpp"

#include <OBSApp.hpp>

#include <QAction>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "moc_StatusPanel.cpp"

namespace OBS::Accessibility {

namespace {
class StatusListWidget : public QListWidget {
public:
	using QListWidget::QListWidget;
	std::function<void()> refresh;
	QMenu *menu = nullptr;

protected:
	void focusInEvent(QFocusEvent *event) override
	{
		refresh();
		QListWidget::focusInEvent(event);
	}

	void keyPressEvent(QKeyEvent *event) override
	{
		refresh();
		QListWidget::keyPressEvent(event);
	}

	void contextMenuEvent(QContextMenuEvent *event) override { ExecContextMenu(this, menu, event); }
};
} // namespace

StatusPanel::StatusPanel(QWidget *parent) : QGroupBox(QTStr("Basic.Stats.Status"), parent)
{
	StatusListWidget *list = new StatusListWidget(this);
	list->refresh = [this]() { Refresh(); };
	view = list;
	view->setAccessibleName(QTStr("Basic.Stats.Status"));
	view->setSelectionMode(QAbstractItemView::SingleSelection);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(view);

	menu = new QMenu(QTStr("Accessibility.Menu.Status"), this);
	for (const Readout &readout : GetReadouts()) {
		QAction *action = menu->addAction(QTStr(readout.lookupKey));
		auto text = readout.text;
		connect(action, &QAction::triggered, this, [text]() { AnnounceRequested(text()); });
	}
	list->menu = menu;

	connect(view, &QListWidget::itemActivated, this, &StatusPanel::ReadCurrentLine);

	if (StatusMonitor *monitor = StatusMonitor::Get()) {
		connect(monitor, &StatusMonitor::Updated, this, [this]() {
			if (!view->hasFocus()) {
				Refresh();
			}
		});
	}

	Refresh();
}

void StatusPanel::Refresh()
{
	StatusMonitor *monitor = StatusMonitor::Get();
	if (!monitor) {
		return;
	}

	QStringList lines = monitor->Lines();

	if (view->count() != lines.size()) {
		int currentRow = view->currentRow();
		view->clear();
		view->addItems(lines);
		view->setCurrentRow(std::clamp(currentRow, 0, static_cast<int>(lines.size()) - 1));
		return;
	}

	for (int row = 0; row < view->count(); row++) {
		QListWidgetItem *item = view->item(row);
		if (item->text() != lines[row]) {
			item->setText(lines[row]);
		}
	}
}

void StatusPanel::ReadCurrentLine()
{
	Refresh();
	if (QListWidgetItem *item = view->currentItem()) {
		AnnounceRequested(item->text());
	}
}

} // namespace OBS::Accessibility
