#include "StreamDeckDialog.hpp"
#include "AccessibleCommon.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

#include "moc_StreamDeckDialog.cpp"

namespace OBS::Accessibility {

namespace {
QPointer<StreamDeckDialog> openDialog;

bool SameHotkey(const StreamDeck::HotkeyRef &a, const StreamDeck::HotkeyRef &b)
{
	return a.owner == b.owner && a.context == b.context && a.name == b.name;
}
} // namespace

StreamDeckDialog::StreamDeckDialog(StreamDeckManager *manager_, QWidget *parent)
	: QDialog(parent),
	  manager(manager_),
	  mapping(manager_->GetMapping())
{
	setWindowTitle(QTStr("Accessibility.StreamDeck.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	status = new QLineEdit(this);
	status->setReadOnly(true);

	enabled = new QCheckBox(QTStr("Accessibility.StreamDeck.Enabled"), this);
	enabled->setChecked(manager->IsEnabled());

	QLabel *keysLabel = new QLabel(QTStr("Accessibility.StreamDeck.Keys"), this);
	keys = new QListWidget(this);
	keysLabel->setBuddy(keys);

	label = new QLineEdit(this);

	assignButton = new QPushButton(QTStr("Accessibility.StreamDeck.Assign"), this);
	removeButton = new QPushButton(QTStr("Accessibility.StreamDeck.Remove"), this);
	tryButton = new QPushButton(QTStr("Accessibility.StreamDeck.Try"), this);

	announceActions = new QCheckBox(QTStr("Accessibility.StreamDeck.AnnounceActions"), this);
	announceActions->setChecked(manager->GetAnnounceActions());

	brightness = new QSpinBox(this);
	brightness->setRange(0, 100);
	brightness->setSuffix(" %");
	brightness->setValue(manager->GetBrightness());

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	QFormLayout *statusLayout = new QFormLayout;
	statusLayout->addRow(QTStr("Accessibility.StreamDeck.Status"), status);

	QFormLayout *labelLayout = new QFormLayout;
	labelLayout->addRow(QTStr("Accessibility.StreamDeck.Label"), label);

	QHBoxLayout *keyButtons = new QHBoxLayout;
	keyButtons->addWidget(assignButton);
	keyButtons->addWidget(removeButton);
	keyButtons->addWidget(tryButton);
	keyButtons->addStretch();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(statusLayout);
	layout->addWidget(enabled);
	layout->addWidget(keysLabel);
	layout->addWidget(keys);
	layout->addLayout(labelLayout);
	layout->addLayout(keyButtons);
	layout->addWidget(announceActions);

	QFormLayout *brightnessLayout = new QFormLayout;
	brightnessLayout->addRow(QTStr("Accessibility.StreamDeck.Brightness"), brightness);
	layout->addLayout(brightnessLayout);
	layout->addWidget(buttons);

	/* Enter on a key assigns it rather than closing the dialog. */
	for (QPushButton *button : findChildren<QPushButton *>()) {
		button->setAutoDefault(false);
	}

	connect(keys, &QListWidget::currentRowChanged, this, [this]() {
		int key = CurrentKey();
		auto assignment = mapping.find(key);
		label->setText(assignment != mapping.end() ? assignment->second.label : QString());
		UpdateButtons();
	});
	connect(keys, &QListWidget::itemActivated, this, &StreamDeckDialog::Assign);
	connect(label, &QLineEdit::textEdited, this, [this](const QString &text) {
		auto assignment = mapping.find(CurrentKey());
		if (assignment != mapping.end()) {
			assignment->second.label = text;
		}
	});
	connect(assignButton, &QPushButton::clicked, this, &StreamDeckDialog::Assign);
	connect(removeButton, &QPushButton::clicked, this, &StreamDeckDialog::Remove);
	connect(tryButton, &QPushButton::clicked, this, &StreamDeckDialog::Try);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		Save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	/* The keys show each brightness at once, and go back to the saved
	 * one on Cancel. */
	connect(brightness, &QSpinBox::valueChanged, this, [this](int value) { manager->SetBrightness(value, false); });
	connect(this, &QDialog::rejected, this, [this]() { manager->SetBrightness(manager->GetBrightness(), false); });

	connect(manager, &StreamDeckManager::KeyChanged, this, [this](int key, bool pressed) {
		if (pressed) {
			HandlePhysicalKey(key);
		}
	});
	connect(manager, &StreamDeckManager::ConnectionChanged, this, [this]() {
		UpdateStatus();
		Refresh();
	});

	manager->SetCaptured(true);

	UpdateStatus();
	Refresh();
	keys->setCurrentRow(0);
	keys->setFocus();
	resize(560, 420);
}

StreamDeckDialog::~StreamDeckDialog()
{
	if (manager) {
		manager->SetCaptured(false);
	}
}

void StreamDeckDialog::Open(QWidget *parent)
{
	if (openDialog) {
		openDialog->raise();
		openDialog->activateWindow();
		return;
	}

	StreamDeckManager *manager = StreamDeckManager::Get();
	if (!manager) {
		return;
	}

	openDialog = new StreamDeckDialog(manager, parent);
	openDialog->setAttribute(Qt::WA_DeleteOnClose);
	openDialog->open();
}

void StreamDeckDialog::UpdateStatus()
{
	QString text;
	if (const StreamDeck::Model *model = manager->ConnectedModel()) {
		text = QTStr("Accessibility.StreamDeck.Connected").arg(QT_UTF8(model->name));
		if (StreamDeck::IsElgatoAppRunning()) {
			text += ". " + QTStr("Accessibility.StreamDeck.ElgatoAppRunning");
		}
	} else {
		text = QTStr("Accessibility.StreamDeck.NotFound").arg(QT_UTF8(manager->ConfiguredModel().name));
	}

	status->setText(text);
	status->setCursorPosition(0);
}

void StreamDeckDialog::Refresh()
{
	int row = std::max(keys->currentRow(), 0);
	int keyCount = manager->ConfiguredModel().keyCount;

	keys->clear();
	for (int key = 0; key < keyCount; key++) {
		keys->addItem(RowText(key));
	}
	keys->setCurrentRow(std::min(row, keyCount - 1));
}

QString StreamDeckDialog::RowText(int key) const
{
	auto assignment = mapping.find(key);
	QString action = assignment != mapping.end() ? StreamDeck::Describe(assignment->second.action)
						     : QTStr("Accessibility.StreamDeck.NoAction");

	return QTStr("Accessibility.StreamDeck.KeyRow")
		.arg(StreamDeck::KeyName(manager->ConfiguredModel(), key), action);
}

int StreamDeckDialog::CurrentKey() const
{
	return keys->currentRow();
}

void StreamDeckDialog::UpdateButtons()
{
	bool assigned = mapping.count(CurrentKey()) > 0;
	assignButton->setEnabled(CurrentKey() >= 0);
	removeButton->setEnabled(assigned);
	tryButton->setEnabled(assigned);
	label->setEnabled(assigned);
}

/* Moves to the key that was pressed. A screen reader speaks the new row; if
 * the row does not change, the row is spoken here instead. */
void StreamDeckDialog::HandlePhysicalKey(int key)
{
	if (choosingAction || key >= keys->count()) {
		return;
	}

	if (!keys->hasFocus()) {
		keys->setCurrentRow(key);
		keys->setFocus();
	} else if (keys->currentRow() != key) {
		keys->setCurrentRow(key);
	} else {
		AnnounceRequested(RowText(key));
	}
}

void StreamDeckDialog::Assign()
{
	int key = CurrentKey();
	if (key < 0) {
		return;
	}

	auto assignment = mapping.find(key);
	StreamDeck::HotkeyRef current = assignment != mapping.end() ? assignment->second.action
								    : StreamDeck::HotkeyRef();

	auto picker = new StreamDeckActionPicker(StreamDeck::KeyName(manager->ConfiguredModel(), key), current, this);
	picker->setAttribute(Qt::WA_DeleteOnClose);
	choosingAction = true;

	connect(picker, &QDialog::finished, this, [this]() {
		choosingAction = false;
		keys->setFocus();
	});
	connect(picker, &QDialog::accepted, this, [this, picker, key]() {
		StreamDeck::KeyAssignment &changed = mapping[key];
		QString previousDescription =
			changed.action.IsValid() ? StreamDeck::Describe(changed.action) : QString();
		changed.action = picker->SelectedAction();

		/* A label that only repeated the old action follows the new
		 * one; a label the user wrote stays. */
		if (changed.label.isEmpty() || changed.label == previousDescription) {
			changed.label = picker->SelectedDescription();
		}

		keys->item(key)->setText(RowText(key));
		label->setText(changed.label);
		UpdateButtons();
	});

	picker->open();
}

void StreamDeckDialog::Remove()
{
	int key = CurrentKey();
	if (mapping.erase(key) == 0) {
		return;
	}

	keys->item(key)->setText(RowText(key));
	label->clear();
	UpdateButtons();
	keys->setFocus();
}

void StreamDeckDialog::Try()
{
	auto assignment = mapping.find(CurrentKey());
	if (assignment == mapping.end()) {
		return;
	}

	StreamDeck::ResolvedAction resolved = StreamDeck::Resolve(assignment->second.action);
	if (!resolved.IsValid()) {
		AnnounceRequested(StreamDeck::Describe(assignment->second.action));
		return;
	}
	StreamDeck::Toggle(resolved);

	if (announceActions->isChecked()) {
		if (Announcer *announcer = Announcer::Get()) {
			announcer->AnnounceAtOnce(
				StreamDeck::Feedback(assignment->second.action, assignment->second.label));
		}
	}
}

void StreamDeckDialog::Save()
{
	manager->SetMapping(mapping);
	manager->SetEnabled(enabled->isChecked());
	manager->SetAnnounceActions(announceActions->isChecked());
	manager->SetBrightness(brightness->value(), true);
}

StreamDeckActionPicker::StreamDeckActionPicker(const QString &keyName, const StreamDeck::HotkeyRef &current,
					       QWidget *parent)
	: QDialog(parent),
	  actions(StreamDeck::ListActions())
{
	setWindowTitle(QTStr("Accessibility.StreamDeck.Picker.Title").arg(keyName));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	QLabel *searchLabel = new QLabel(QTStr("Accessibility.StreamDeck.Picker.Search"), this);
	search = new QLineEdit(this);
	searchLabel->setBuddy(search);

	QLabel *treeLabel = new QLabel(QTStr("Accessibility.StreamDeck.Picker.Actions"), this);
	tree = new QTreeWidget(this);
	tree->setHeaderHidden(true);
	tree->setColumnCount(1);
	treeLabel->setBuddy(tree);

	toggle = new QCheckBox(this);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	okButton = buttons->button(QDialogButtonBox::Ok);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(searchLabel);
	layout->addWidget(search);
	layout->addWidget(treeLabel);
	layout->addWidget(tree);
	layout->addWidget(toggle);
	layout->addWidget(buttons);

	std::map<StreamDeck::ActionCategory, QTreeWidgetItem *> categories;
	QTreeWidgetItem *currentItem = nullptr;
	for (size_t i = 0; i < actions.size(); i++) {
		const StreamDeck::ActionInfo &action = actions[i];

		QTreeWidgetItem *&category = categories[action.category];
		if (!category) {
			category = new QTreeWidgetItem(tree, {StreamDeck::CategoryName(action.category)});
			category->setData(0, Qt::UserRole, -1);
		}

		QTreeWidgetItem *item = new QTreeWidgetItem(category, {action.description});
		item->setData(0, Qt::UserRole, static_cast<int>(i));
		if (current.IsValid() && SameHotkey(action.ref, current)) {
			currentItem = item;
		}
	}
	tree->expandAll();
	KeepTreeExpansionAccessible(tree);

	connect(search, &QLineEdit::textChanged, this, &StreamDeckActionPicker::Filter);
	connect(tree, &QTreeWidget::currentItemChanged, this, [this]() {
		const StreamDeck::ActionInfo *action = CurrentAction();
		toggle->setChecked(action && !action->partnerDescription.isEmpty());
		UpdateToggle();
	});
	connect(tree, &QTreeWidget::itemActivated, this, [this]() {
		if (CurrentAction()) {
			accept();
		}
	});
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	if (currentItem) {
		tree->setCurrentItem(currentItem);
		toggle->setChecked(current.toggle && toggle->isEnabled());
	} else if (tree->topLevelItemCount() > 0) {
		QTreeWidgetItem *first = tree->topLevelItem(0);
		tree->setCurrentItem(first->childCount() > 0 ? first->child(0) : first);
	}
	UpdateToggle();

	tree->setFocus();
	resize(560, 500);
}

const StreamDeck::ActionInfo *StreamDeckActionPicker::CurrentAction() const
{
	QTreeWidgetItem *item = tree->currentItem();
	int index = item ? item->data(0, Qt::UserRole).toInt() : -1;
	if (index < 0 || index >= static_cast<int>(actions.size())) {
		return nullptr;
	}
	return &actions[static_cast<size_t>(index)];
}

StreamDeck::HotkeyRef StreamDeckActionPicker::SelectedAction() const
{
	const StreamDeck::ActionInfo *action = CurrentAction();
	if (!action) {
		return {};
	}

	StreamDeck::HotkeyRef ref = action->ref;
	ref.toggle = toggle->isEnabled() && toggle->isChecked();
	return ref;
}

QString StreamDeckActionPicker::SelectedDescription() const
{
	return StreamDeck::Describe(SelectedAction());
}

void StreamDeckActionPicker::Filter(const QString &text)
{
	QTreeWidgetItem *firstMatch = nullptr;

	for (int i = 0; i < tree->topLevelItemCount(); i++) {
		QTreeWidgetItem *category = tree->topLevelItem(i);
		bool anyVisible = false;

		for (int j = 0; j < category->childCount(); j++) {
			QTreeWidgetItem *item = category->child(j);
			bool visible = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
			item->setHidden(!visible);
			if (visible && !firstMatch) {
				firstMatch = item;
			}
			anyVisible = anyVisible || visible;
		}
		category->setHidden(!anyVisible);
	}

	QTreeWidgetItem *current = tree->currentItem();
	if (!current || current->isHidden() || (current->parent() && current->parent()->isHidden())) {
		tree->setCurrentItem(firstMatch);
	}
	UpdateToggle();
}

void StreamDeckActionPicker::UpdateToggle()
{
	const StreamDeck::ActionInfo *action = CurrentAction();
	bool paired = action && !action->partnerDescription.isEmpty();

	toggle->setEnabled(paired);
	toggle->setText(paired ? QTStr("Accessibility.StreamDeck.Picker.Toggle").arg(action->partnerDescription)
			       : QTStr("Accessibility.StreamDeck.Picker.ToggleUnavailable"));
	okButton->setEnabled(action != nullptr);
}

} // namespace OBS::Accessibility
