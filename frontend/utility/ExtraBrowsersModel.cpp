#include "ExtraBrowsersModel.hpp"

#include <accessibility/Announcer.hpp>
#include <components/DelButton.hpp>
#include <docks/BrowserDock.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QKeyEvent>
#include <QTableView>

#include "moc_ExtraBrowsersModel.cpp"

void ExtraBrowsersModel::Reset()
{
	items.clear();

	OBSBasic *main = OBSBasic::Get();

	for (int i = 0; i < main->extraBrowserDocks.size(); i++) {
		Item item;
		item.prevIdx = i;
		item.title = main->extraBrowserDockNames[i];
		item.url = main->extraBrowserDockTargets[i];
		items.push_back(item);
	}
}

int ExtraBrowsersModel::rowCount(const QModelIndex &) const
{
	int count = items.size() + 1;
	return count;
}

int ExtraBrowsersModel::columnCount(const QModelIndex &) const
{
	return (int)Column::Count;
}

QVariant ExtraBrowsersModel::data(const QModelIndex &index, int role) const
{
	int column = index.column();
	int idx = index.row();
	int count = items.size();
	bool validRole = role == Qt::DisplayRole || role == Qt::AccessibleTextRole;

	if (!validRole) {
		return QVariant();
	}

	if (idx >= 0 && idx < count) {
		switch (column) {
		case (int)Column::Title:
			return items[idx].title;
		case (int)Column::Url:
			return items[idx].url;
		}
	} else if (idx == count) {
		switch (column) {
		case (int)Column::Title:
			return newTitle;
		case (int)Column::Url:
			return newURL;
		}
	}

	return QVariant();
}

QVariant ExtraBrowsersModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	bool validRole = role == Qt::DisplayRole || role == Qt::AccessibleTextRole;

	if (validRole && orientation == Qt::Orientation::Horizontal) {
		switch (section) {
		case (int)Column::Title:
			return QTStr("ExtraBrowsers.DockName");
		case (int)Column::Url:
			return QStringLiteral("URL");
		}
	}

	return QVariant();
}

Qt::ItemFlags ExtraBrowsersModel::flags(const QModelIndex &index) const
{
	Qt::ItemFlags flags = QAbstractTableModel::flags(index);

	if (index.column() != (int)Column::Delete) {
		flags |= Qt::ItemIsEditable;
	}

	return flags;
}
void ExtraBrowsersModel::AddDeleteButton(int idx)
{
	QTableView *widget = reinterpret_cast<QTableView *>(parent());

	QModelIndex index = createIndex(idx, (int)Column::Delete, nullptr);

	QPushButton *del = new DelButton(index);
	del->setProperty("class", "icon-trash");
	del->setObjectName("extraPanelDelete");
	del->setMinimumSize(QSize(20, 20));
	del->setAccessibleName(QTStr("Remove") + " " + items[idx].title);
	del->installEventFilter(this);
	connect(del, &QPushButton::clicked, this, &ExtraBrowsersModel::DeleteItem);

	widget->setIndexWidget(index, del);
	widget->setRowHeight(idx, 20);
	widget->setColumnWidth(idx, 20);
}

void ExtraBrowsersModel::CheckToAdd()
{
	if (newTitle.isEmpty() || newURL.isEmpty()) {
		return;
	}

	int idx = items.size() + 1;
	beginInsertRows(QModelIndex(), idx, idx);

	Item item;
	item.prevIdx = -1;
	item.title = newTitle;
	item.url = newURL;
	items.push_back(item);

	newTitle = "";
	newURL = "";

	endInsertRows();

	AddDeleteButton(idx - 1);
}

void ExtraBrowsersModel::UpdateItem(Item &item)
{
	int idx = item.prevIdx;

	OBSBasic *main = OBSBasic::Get();
	BrowserDock *dock = reinterpret_cast<BrowserDock *>(main->extraBrowserDocks[idx].get());
	dock->setWindowTitle(item.title);
	dock->setObjectName(item.title + OBJ_NAME_SUFFIX);

	if (main->extraBrowserDockNames[idx] != item.title) {
		main->extraBrowserDockNames[idx] = item.title;
		dock->toggleViewAction()->setText(item.title);
		dock->setTitle(item.title);
	}

	if (main->extraBrowserDockTargets[idx] != item.url) {
		dock->cefWidget->setURL(QT_TO_UTF8(item.url));
		main->extraBrowserDockTargets[idx] = item.url;
	}
}

void ExtraBrowsersModel::DeleteItem()
{
	DelButton *del = reinterpret_cast<DelButton *>(sender());
	DeleteRow(del->index.row());
}

void ExtraBrowsersModel::DeleteRow(int row)
{
	QTableView *widget = reinterpret_cast<QTableView *>(parent());

	if (row < 0 || row >= items.size()) {
		return;
	}

	/* there's some sort of internal bug in Qt and deleting certain index
	 * widgets or "editors" that can cause a crash inside Qt if the widget
	 * is not manually removed, at least on 5.7 */
	QModelIndex delIndex = index(row, (int)Column::Delete);
	if (QWidget *del = widget->indexWidget(delIndex)) {
		widget->setIndexWidget(delIndex, nullptr);
		del->deleteLater();
	}

	QString title = items[row].title;

	/* --------- */

	beginRemoveRows(QModelIndex(), row, row);

	int prevIdx = items[row].prevIdx;
	items.removeAt(row);

	if (prevIdx != -1) {
		int i = 0;
		for (; i < deleted.size() && deleted[i] < prevIdx; i++)
			;
		deleted.insert(i, prevIdx);
	}

	endRemoveRows();

	if (auto announcer = OBS::Accessibility::Announcer::Get()) {
		announcer->Announce(QTStr("Accessibility.Docks.BrowserDockRemoved").arg(title),
				    OBS::Accessibility::Priority::High);
	}

	/* The focused button or field went away with the row, so continue
	 * from the row that took its place, or from the empty row. */
	FocusCell(row, (int)Column::Title);
}

void ExtraBrowsersModel::FocusCell(int row, int column)
{
	QTableView *widget = reinterpret_cast<QTableView *>(parent());
	QItemSelectionModel *selModel = widget->selectionModel();

	/* Setting the current cell opens its editor and focuses it. */
	selModel->clearCurrentIndex();
	selModel->setCurrentIndex(index(row, column), QItemSelectionModel::Clear);
}

void ExtraBrowsersModel::FocusDeleteButton(int row)
{
	QTableView *widget = reinterpret_cast<QTableView *>(parent());
	QModelIndex delIndex = index(row, (int)Column::Delete);

	QWidget *del = widget->indexWidget(delIndex);
	if (!del) {
		return;
	}

	widget->selectionModel()->setCurrentIndex(delIndex, QItemSelectionModel::Clear);
	del->setFocus(Qt::TabFocusReason);
}

void ExtraBrowsersModel::UpdateDeleteButtonName(int row)
{
	QTableView *widget = reinterpret_cast<QTableView *>(parent());
	if (QWidget *del = widget->indexWidget(index(row, (int)Column::Delete))) {
		del->setAccessibleName(QTStr("Remove") + " " + items[row].title);
	}
}

/* The delete buttons take part in the table's Tab order, after the URL of
 * their row, and Delete removes their dock. */
bool ExtraBrowsersModel::eventFilter(QObject *object, QEvent *event)
{
	DelButton *del = qobject_cast<DelButton *>(object);
	if (!del || event->type() != QEvent::KeyPress) {
		return false;
	}

	int row = del->index.row();

	switch (static_cast<QKeyEvent *>(event)->key()) {
	case Qt::Key_Tab:
		FocusCell(row + 1, (int)Column::Title);
		return true;
	case Qt::Key_Backtab:
		FocusCell(row, (int)Column::Url);
		return true;
	case Qt::Key_Delete:
		DeleteRow(row);
		return true;
	default:
		return false;
	}
}

void ExtraBrowsersModel::Apply()
{
	OBSBasic *main = OBSBasic::Get();

	for (Item &item : items) {
		if (item.prevIdx != -1) {
			UpdateItem(item);
		} else {
			QString uuid = QUuid::createUuid().toString();
			uuid.replace(QRegularExpression("[{}-]"), "");
			main->AddExtraBrowserDock(item.title, item.url, uuid, true);
		}
	}

	for (int i = deleted.size() - 1; i >= 0; i--) {
		int idx = deleted[i];
		main->extraBrowserDockTargets.removeAt(idx);
		main->extraBrowserDockNames.removeAt(idx);
		main->extraBrowserDocks.removeAt(idx);
	}

	if (main->extraBrowserDocks.empty()) {
		main->extraBrowserMenuDocksSeparator.clear();
	}

	deleted.clear();

	Reset();
}

/* Moves focus from the table to the next or previous control of the dialog,
 * so the keyboard can reach its buttons. */
static void LeaveTable(QWidget *table, bool forward)
{
	QWidget *next = table;
	do {
		next = forward ? next->nextInFocusChain() : next->previousInFocusChain();
	} while (next != table && (table->isAncestorOf(next) || !next->isVisible() || !next->isEnabled() ||
				   (next->focusPolicy() & Qt::TabFocus) != Qt::TabFocus));

	if (next != table) {
		next->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
	}
}

void ExtraBrowsersModel::TabSelection(bool forward)
{
	QListView *widget = reinterpret_cast<QListView *>(parent());
	QItemSelectionModel *selModel = widget->selectionModel();

	QModelIndex sel = selModel->currentIndex();
	int row = sel.row();
	int col = sel.column();

	switch (sel.column()) {
	case (int)Column::Title:
		if (!forward) {
			if (row == 0) {
				LeaveTable(widget, false);
				return;
			}

			FocusDeleteButton(row - 1);
			return;
		}

		col += 1;
		break;

	case (int)Column::Url:
		if (forward) {
			if (row == items.size()) {
				LeaveTable(widget, true);
				return;
			}

			FocusDeleteButton(row);
			return;
		}

		col -= 1;
	}

	sel = createIndex(row, col, nullptr);
	selModel->setCurrentIndex(sel, QItemSelectionModel::Clear);
}

void ExtraBrowsersModel::Init()
{
	for (int i = 0; i < items.count(); i++) {
		AddDeleteButton(i);
	}
}
