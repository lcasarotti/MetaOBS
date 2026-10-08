#include "CommandPalette.hpp"
#include "AccessibleCommon.hpp"

#include <OBSApp.hpp>

#include <QAction>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "moc_CommandPalette.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int announceDelayMs = 500;
constexpr int commandIndexRole = Qt::UserRole;
constexpr const char *excludedProperty = "accessibilityPaletteExcluded";

/* Menu texts without their mnemonics ("&File") or shortcuts after a tab. */
QString PlainText(const QString &text)
{
	QString plain = text.section('\t', 0, 0);
	plain.replace("&&", QChar(0x1));
	plain.remove('&');
	plain.replace(QChar(0x1), "&");
	return plain.trimmed();
}

void CollectMenu(QMenu *menu, const QString &path, QSet<QMenu *> &visited,
		 const std::function<void(QAction *, const QString &)> &add)
{
	if (!menu || visited.contains(menu)) {
		return;
	}
	visited.insert(menu);

	/* Menus fill or update some of their items just before they open. */
	emit menu->aboutToShow();

	for (QAction *action : menu->actions()) {
		if (action->isSeparator() || !action->isVisible() || !action->isEnabled() ||
		    action->property(excludedProperty).toBool()) {
			continue;
		}

		QString name = PlainText(action->text());
		if (name.isEmpty()) {
			continue;
		}

		if (QMenu *submenu = action->menu()) {
			CollectMenu(submenu, path + " > " + name, visited, add);
		} else {
			add(action, path);
		}
	}
}
} // namespace

CommandPalette::CommandPalette(QMenuBar *menuBar, QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QTStr("Accessibility.Palette.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	QLabel *searchLabel = new QLabel(QTStr("Accessibility.Palette.Search"), this);
	search = new QLineEdit(this);
	searchLabel->setBuddy(search);
	search->setAccessibleDescription(QTStr("Accessibility.Palette.SearchHint"));

	list = new QListWidget(this);
	list->setAccessibleName(QTStr("Accessibility.Palette.List"));

	/* Enter runs the selected command, not Cancel. */
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(searchLabel);
	layout->addWidget(search);
	layout->addWidget(list);
	layout->addWidget(buttons);

	announceTimer = new QTimer(this);
	announceTimer->setSingleShot(true);
	announceTimer->setInterval(announceDelayMs);

	connect(search, &QLineEdit::textChanged, this, &CommandPalette::Filter);
	connect(search, &QLineEdit::textChanged, announceTimer, qOverload<>(&QTimer::start));
	connect(announceTimer, &QTimer::timeout, this, &CommandPalette::AnnounceCount);
	connect(list, &QListWidget::itemDoubleClicked, this, &CommandPalette::Choose);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	search->installEventFilter(this);
	list->installEventFilter(this);

	QSet<QMenu *> visited;
	auto add = [this](QAction *action, const QString &path) {
		QString name = PlainText(action->text());
		QString text = QTStr("Accessibility.Palette.Command").arg(name, path);

		QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
		if (!shortcut.isEmpty()) {
			text += ", " + shortcut;
		}
		if (action->isCheckable() && action->isChecked()) {
			text += ", " + QTStr("Accessibility.Palette.Checked");
		}

		commands.append({action, text, (name + " " + path).toLower()});
	};

	for (QAction *action : menuBar->actions()) {
		if (QMenu *menu = action->menu(); menu && action->isVisible() && action->isEnabled()) {
			CollectMenu(menu, PlainText(menu->title()), visited, add);
		}
	}

	Filter();
	search->setFocus();
	resize(600, 450);
}

void CommandPalette::Exclude(QAction *action)
{
	action->setProperty(excludedProperty, true);
}

void CommandPalette::Run(QMenuBar *menuBar, QWidget *parent)
{
	CommandPalette *palette = new CommandPalette(menuBar, parent);
	palette->setAttribute(Qt::WA_DeleteOnClose);

	/* Run the command once focus is back where it was, since many commands
	 * act on the selected item of the focused panel. */
	connect(palette, &QDialog::accepted, parent, [palette]() {
		if (QAction *action = palette->ChosenAction()) {
			QTimer::singleShot(0, action, &QAction::trigger);
		}
	});

	palette->open();
}

void CommandPalette::Filter()
{
	QStringList words = search->text().toLower().split(' ', Qt::SkipEmptyParts);

	list->clear();
	for (int i = 0; i < commands.size(); i++) {
		const Command &command = commands[i];
		if (!command.action) {
			continue;
		}

		bool matches = std::all_of(words.begin(), words.end(), [&command](const QString &word) {
			return command.searchText.contains(word);
		});
		if (!matches) {
			continue;
		}

		QListWidgetItem *item = new QListWidgetItem(command.text, list);
		item->setData(commandIndexRole, i);
	}

	if (list->count() > 0) {
		list->setCurrentRow(0);
	}
}

void CommandPalette::AnnounceCount()
{
	int count = list->count();
	if (count == 0) {
		AnnounceRequested(QTStr("Accessibility.Palette.None"));
	} else if (count == 1) {
		AnnounceRequested(QTStr("Accessibility.Palette.One").arg(list->item(0)->text()));
	} else {
		AnnounceRequested(QTStr("Accessibility.Palette.Count").arg(count));
	}
}

void CommandPalette::Choose()
{
	QListWidgetItem *item = list->currentItem();
	if (!item && list->count() > 0) {
		item = list->item(0);
	}
	if (!item) {
		return;
	}

	chosen = commands[item->data(commandIndexRole).toInt()].action;
	accept();
}

/* From the search field, Down moves to the results and Enter runs the first
 * one; Up from the first result goes back to the search field. Enter is
 * handled here in the list too, so the dialog does not also see it. */
bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() != QEvent::KeyPress) {
		return QDialog::eventFilter(watched, event);
	}

	auto keyEvent = static_cast<QKeyEvent *>(event);
	int key = keyEvent->key();
	if (keyEvent->modifiers() & ~Qt::KeypadModifier) {
		return QDialog::eventFilter(watched, event);
	}

	if (watched == search) {
		if ((key == Qt::Key_Down || key == Qt::Key_PageDown) && list->count() > 0) {
			announceTimer->stop();
			list->setFocus(Qt::TabFocusReason);
			return true;
		}
		if (key == Qt::Key_Return || key == Qt::Key_Enter) {
			Choose();
			return true;
		}
	} else if (watched == list) {
		if (key == Qt::Key_Up && list->currentRow() <= 0) {
			search->setFocus(Qt::TabFocusReason);
			return true;
		}
		if (key == Qt::Key_Return || key == Qt::Key_Enter) {
			Choose();
			return true;
		}
	}

	return QDialog::eventFilter(watched, event);
}

} // namespace OBS::Accessibility
