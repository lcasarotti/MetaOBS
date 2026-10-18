#include "MidiDialog.hpp"
#include "AccessibleCommon.hpp"
#include "MidiTargets.hpp"

#include <OBSApp.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "moc_MidiDialog.cpp"

namespace OBS::Accessibility {

namespace {
QPointer<MidiDialog> openDialog;

/* A value of the output choice that picks the device's own output port. */
const QString automaticOutput = QStringLiteral("\x01");
/* A row is announced again at most this often while its control moves. */
constexpr int rowAnnouncementMilliseconds = 1500;

bool IsRelease(const Midi::Message &message)
{
	return message.type == Midi::MessageType::NoteOff ||
	       (message.type == Midi::MessageType::NoteOn && message.value == 0);
}

struct AudioSource {
	QString uuid;
	QString name;
};

std::vector<AudioSource> AudioSources()
{
	std::vector<AudioSource> sources;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			bool audio = (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
			if (audio && !obs_scene_from_source(source) && !obs_group_from_source(source)) {
				static_cast<std::vector<AudioSource> *>(data)->push_back(
					{QT_UTF8(obs_source_get_uuid(source)), QT_UTF8(obs_source_get_name(source))});
			}
			return true;
		},
		&sources);

	std::sort(sources.begin(), sources.end(), [](const AudioSource &a, const AudioSource &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return sources;
}
} // namespace

MidiDialog::MidiDialog(MidiManager *manager_, QWidget *parent)
	: QDialog(parent),
	  manager(manager_),
	  mapping(manager_->GetMapping()),
	  portSettings(manager_->GetPortSettings())
{
	setWindowTitle(QTStr("Accessibility.Midi.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	status = new QLineEdit(this);
	status->setReadOnly(true);

	enabled = new QCheckBox(QTStr("Accessibility.Midi.Enabled"), this);
	enabled->setChecked(manager->IsEnabled());

	QLabel *portsLabel = new QLabel(QTStr("Accessibility.Midi.Ports"), this);
	ports = new QListWidget(this);
	portsLabel->setBuddy(ports);

	output = new QComboBox(this);

	QLabel *assignmentsLabel = new QLabel(QTStr("Accessibility.Midi.Assignments"), this);
	assignments = new QListWidget(this);
	assignmentsLabel->setBuddy(assignments);

	assignButton = new QPushButton(QTStr("Accessibility.Midi.Assign"), this);
	optionsButton = new QPushButton(QTStr("Accessibility.Midi.Options"), this);
	removeButton = new QPushButton(QTStr("Accessibility.Midi.Remove"), this);

	announceValues = new QCheckBox(QTStr("Accessibility.Midi.AnnounceValues"), this);
	announceValues->setChecked(manager->GetAnnounceValues());

	QLabel *hint = new QLabel(QTStr("Accessibility.Midi.LearnHint"), this);
	hint->setWordWrap(true);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	QFormLayout *statusLayout = new QFormLayout;
	statusLayout->addRow(QTStr("Accessibility.Midi.Status"), status);

	QFormLayout *outputLayout = new QFormLayout;
	outputLayout->addRow(QTStr("Accessibility.Midi.Output"), output);

	QHBoxLayout *assignmentButtons = new QHBoxLayout;
	assignmentButtons->addWidget(assignButton);
	assignmentButtons->addWidget(optionsButton);
	assignmentButtons->addWidget(removeButton);
	assignmentButtons->addStretch();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(statusLayout);
	layout->addWidget(enabled);
	layout->addWidget(portsLabel);
	layout->addWidget(ports);
	layout->addLayout(outputLayout);
	layout->addWidget(assignmentsLabel);
	layout->addWidget(assignments, 1);
	layout->addLayout(assignmentButtons);
	layout->addWidget(announceValues);
	layout->addWidget(hint);
	layout->addWidget(buttons);

	/* Enter on a row assigns it rather than closing the dialog. */
	for (QPushButton *button : findChildren<QPushButton *>()) {
		button->setAutoDefault(false);
	}

	connect(ports, &QListWidget::currentRowChanged, this, &MidiDialog::UpdateOutputChoice);
	connect(ports, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		portSettings.SetInputEnabled(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
	});
	connect(output, &QComboBox::activated, this, [this]() {
		QListWidgetItem *item = ports->currentItem();
		if (!item) {
			return;
		}
		QString input = item->data(Qt::UserRole).toString();
		QString choice = output->currentData().toString();
		if (choice == automaticOutput) {
			portSettings.outputs.erase(input);
		} else {
			portSettings.outputs[input] = choice;
		}
		int row = ports->currentRow();
		RefreshPorts();
		ports->setCurrentRow(row);
	});

	connect(assignments, &QListWidget::currentRowChanged, this, &MidiDialog::UpdateButtons);
	connect(assignments, &QListWidget::itemActivated, this, &MidiDialog::Assign);
	connect(assignButton, &QPushButton::clicked, this, &MidiDialog::Assign);
	connect(optionsButton, &QPushButton::clicked, this, &MidiDialog::EditOptions);
	connect(removeButton, &QPushButton::clicked, this, &MidiDialog::Remove);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		Save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(manager, &MidiManager::MessageReceived, this, &MidiDialog::HandleMessage);
	connect(manager, &MidiManager::PortsChanged, this, [this]() {
		UpdateStatus();
		RefreshPorts();
	});

	manager->Capture();

	UpdateStatus();
	RefreshPorts();
	ports->setCurrentRow(0);
	RefreshAssignments();
	assignments->setCurrentRow(0);
	UpdateButtons();
	assignments->setFocus();
	resize(620, 560);
}

MidiDialog::~MidiDialog()
{
	if (manager) {
		manager->Release();
	}
}

void MidiDialog::Open(QWidget *parent)
{
	if (openDialog) {
		openDialog->raise();
		openDialog->activateWindow();
		return;
	}

	MidiManager *manager = MidiManager::Get();
	if (!manager) {
		return;
	}

	openDialog = new MidiDialog(manager, parent);
	openDialog->setAttribute(Qt::WA_DeleteOnClose);
	openDialog->open();
}

void MidiDialog::UpdateStatus()
{
	QStringList open;
	for (const QString &input : manager->InputNames()) {
		if (manager->IsInputOpen(input)) {
			open << input;
		}
	}

	QString text;
	if (!manager->IsEnabled()) {
		text = QTStr("Accessibility.Midi.StatusDisabled");
	} else if (open.isEmpty()) {
		text = QTStr("Accessibility.Midi.StatusNone");
	} else {
		text = QTStr("Accessibility.Midi.StatusListening").arg(open.join(QStringLiteral(", ")));
	}

	QStringList failed = manager->FailedInputs();
	if (!failed.isEmpty()) {
		text += ". " + QTStr("Accessibility.Midi.StatusFailed").arg(failed.join(QStringLiteral(", ")));
	}

	status->setText(text);
	status->setCursorPosition(0);
}

void MidiDialog::RefreshPorts()
{
	int row = std::max(ports->currentRow(), 0);
	QSignalBlocker blocker(ports);
	ports->clear();

	for (const QString &input : manager->InputNames()) {
		QString outputName = Midi::OutputFor(portSettings, input, manager->OutputNames());
		QString text = outputName.isEmpty() ? QTStr("Accessibility.Midi.PortNoOutput").arg(input)
						    : QTStr("Accessibility.Midi.PortWithOutput").arg(input, outputName);
		if (manager->FailedInputs().contains(input)) {
			text += ", " + QTStr("Accessibility.Midi.PortInUse");
		}

		QListWidgetItem *item = new QListWidgetItem(text, ports);
		item->setData(Qt::UserRole, input);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(portSettings.IsInputEnabled(input) ? Qt::Checked : Qt::Unchecked);
	}

	if (ports->count() > 0) {
		ports->setCurrentRow(std::min(row, ports->count() - 1));
	}
	blocker.unblock();
	UpdateOutputChoice();
}

void MidiDialog::UpdateOutputChoice()
{
	QSignalBlocker blocker(output);
	output->clear();

	QListWidgetItem *item = ports->currentItem();
	output->setEnabled(item != nullptr);
	if (!item) {
		return;
	}

	QString input = item->data(Qt::UserRole).toString();
	QString paired = Midi::PairedOutput(input, manager->OutputNames());
	output->addItem(paired.isEmpty() ? QTStr("Accessibility.Midi.OutputAutomaticNone")
					 : QTStr("Accessibility.Midi.OutputAutomatic").arg(paired),
			automaticOutput);
	output->addItem(QTStr("Accessibility.Midi.OutputNone"), QString());
	for (const QString &name : manager->OutputNames()) {
		output->addItem(name, name);
	}

	auto chosen = portSettings.outputs.find(input);
	int index = chosen == portSettings.outputs.end() ? 0 : output->findData(chosen->second);
	output->setCurrentIndex(std::max(index, 0));
}

void MidiDialog::RefreshAssignments()
{
	int row = std::max(assignments->currentRow(), 0);
	QSignalBlocker blocker(assignments);
	assignments->clear();

	int rows = static_cast<int>(mapping.size() + unassigned.size());
	for (int i = 0; i < rows; i++) {
		assignments->addItem(RowText(i));
	}
	if (rows == 0) {
		assignments->addItem(QTStr("Accessibility.Midi.NoAssignments"));
	}
	assignments->setCurrentRow(std::min(row, assignments->count() - 1));
	blocker.unblock();
	UpdateButtons();
}

QString MidiDialog::RowText(int row) const
{
	/* The port is named only when the rows come from more than one. */
	Midi::Control control = ControlAt(row);
	bool severalPorts = std::any_of(mapping.begin(), mapping.end(),
					[&control](const Midi::Assignment &other) {
						return other.control.port != control.port;
					}) ||
			    std::any_of(unassigned.begin(), unassigned.end(), [&control](const Midi::Control &other) {
				    return other.port != control.port;
			    });
	QString name = Midi::DescribeControl(control, severalPorts);

	if (row < static_cast<int>(mapping.size())) {
		return QTStr("Accessibility.Midi.Row")
			.arg(name, Midi::DescribeTarget(mapping[static_cast<size_t>(row)].target));
	}
	return QTStr("Accessibility.Midi.Row").arg(name, QTStr("Accessibility.Midi.NoAction"));
}

Midi::Assignment *MidiDialog::AssignmentAt(int row)
{
	if (row < 0 || row >= static_cast<int>(mapping.size())) {
		return nullptr;
	}
	return &mapping[static_cast<size_t>(row)];
}

Midi::Control MidiDialog::ControlAt(int row) const
{
	if (row < 0) {
		return {};
	}
	if (row < static_cast<int>(mapping.size())) {
		return mapping[static_cast<size_t>(row)].control;
	}
	size_t extra = static_cast<size_t>(row) - mapping.size();
	return extra < unassigned.size() ? unassigned[extra] : Midi::Control();
}

void MidiDialog::UpdateButtons()
{
	int row = assignments->currentRow();
	bool assigned = AssignmentAt(row) != nullptr;
	assignButton->setEnabled(ControlAt(row).IsValid());
	optionsButton->setEnabled(assigned);
	removeButton->setEnabled(assigned);
}

/* Moves to the row of the control that was moved, adding it if it is new. A
 * screen reader speaks the new row; if the row does not change, it is spoken
 * here, though not over and over while a fader moves. */
void MidiDialog::HandleMessage(const QString &port, const Midi::Message &message)
{
	if (choosing || IsRelease(message)) {
		return;
	}

	Midi::Control control = Midi::ControlOf(port, message);
	if (!control.IsValid()) {
		return;
	}

	int row = Midi::FindControl(mapping, control);
	if (row < 0) {
		auto found = std::find(unassigned.begin(), unassigned.end(), control);
		if (found == unassigned.end()) {
			unassigned.push_back(control);
			RefreshAssignments();
			found = unassigned.end() - 1;
		}
		row = static_cast<int>(mapping.size() + static_cast<size_t>(found - unassigned.begin()));
	}

	if (!assignments->hasFocus()) {
		assignments->setCurrentRow(row);
		assignments->setFocus();
		lastRowAnnouncement.start();
	} else if (assignments->currentRow() != row) {
		assignments->setCurrentRow(row);
		lastRowAnnouncement.start();
	} else if (!lastRowAnnouncement.isValid() || lastRowAnnouncement.elapsed() > rowAnnouncementMilliseconds) {
		lastRowAnnouncement.start();
		AnnounceRequested(RowText(row));
	}
}

void MidiDialog::Assign()
{
	int row = assignments->currentRow();
	Midi::Control control = ControlAt(row);
	if (!control.IsValid()) {
		return;
	}

	Midi::Assignment *assignment = AssignmentAt(row);
	Midi::Target current = assignment ? assignment->target : Midi::Target();
	bool buttonsOnly = control.type == Midi::ControlType::Note;

	auto picker = new MidiTargetPicker(Midi::DescribeControl(control), current, buttonsOnly, this);
	picker->setAttribute(Qt::WA_DeleteOnClose);
	choosing = true;

	connect(picker, &QDialog::finished, this, [this]() {
		choosing = false;
		assignments->setFocus();
	});
	connect(picker, &QDialog::accepted, this, [this, picker, control]() {
		Midi::Target target = picker->SelectedTarget();
		if (!target.IsValid()) {
			return;
		}

		int index = Midi::FindControl(mapping, control);
		if (index >= 0) {
			Midi::Assignment &changed = mapping[static_cast<size_t>(index)];
			bool kindChanged = changed.target.IsContinuous() != target.IsContinuous();
			changed.target = target;
			if (kindChanged) {
				changed.options = Midi::Options();
			}
		} else {
			Midi::Assignment added;
			added.control = control;
			added.target = target;
			mapping.push_back(added);
			unassigned.erase(std::remove(unassigned.begin(), unassigned.end(), control), unassigned.end());
			index = static_cast<int>(mapping.size()) - 1;
		}

		RefreshAssignments();
		assignments->setCurrentRow(index);
	});

	picker->open();
}

void MidiDialog::EditOptions()
{
	int row = assignments->currentRow();
	Midi::Assignment *assignment = AssignmentAt(row);
	if (!assignment) {
		return;
	}

	auto dialog = new MidiOptionsDialog(*assignment, this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	choosing = true;

	Midi::Control control = assignment->control;
	connect(dialog, &QDialog::finished, this, [this]() {
		choosing = false;
		assignments->setFocus();
	});
	connect(dialog, &QDialog::accepted, this, [this, dialog, control]() {
		int index = Midi::FindControl(mapping, control);
		if (index >= 0) {
			mapping[static_cast<size_t>(index)].options = dialog->SelectedOptions();
		}
	});

	dialog->open();
}

void MidiDialog::Remove()
{
	int row = assignments->currentRow();
	if (!AssignmentAt(row)) {
		return;
	}

	mapping.erase(mapping.begin() + row);
	RefreshAssignments();
	assignments->setFocus();
}

void MidiDialog::Save()
{
	manager->SetEnabled(enabled->isChecked());
	manager->SetAnnounceValues(announceValues->isChecked());
	manager->SetPortSettings(portSettings);
	manager->SetMapping(mapping);
}

MidiTargetPicker::MidiTargetPicker(const QString &controlName, const Midi::Target &current, bool buttonsOnly,
				   QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(QTStr("Accessibility.Midi.Picker.Title").arg(controlName));
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

	/* Each choice remembers its category, to build the tree. */
	std::vector<std::pair<QString, size_t>> categorized;
	auto add = [this, &categorized, buttonsOnly](const QString &category, const Midi::Target &target,
						    const QString &partner = {}) {
		if (buttonsOnly && target.IsContinuous()) {
			return;
		}
		choices.push_back({target, Midi::DescribeTarget(target), partner});
		categorized.emplace_back(category, choices.size() - 1);
	};

	QString audioCategory = QTStr("Accessibility.Midi.Category.Audio");
	for (const AudioSource &source : AudioSources()) {
		for (Midi::TargetKind kind : {Midi::TargetKind::SourceVolume, Midi::TargetKind::SourceBalance,
					      Midi::TargetKind::SourceMute, Midi::TargetKind::SourceMonitoring}) {
			Midi::Target target;
			target.kind = kind;
			target.uuid = source.uuid;
			target.name = source.name;
			add(audioCategory, target);
		}
	}

	QString transitionsCategory = QTStr("Accessibility.Midi.Category.Transitions");
	for (Midi::TargetKind kind : {Midi::TargetKind::TBar, Midi::TargetKind::TransitionDuration}) {
		Midi::Target target;
		target.kind = kind;
		add(transitionsCategory, target);
	}

	for (const StreamDeck::ActionInfo &action : StreamDeck::ListActions()) {
		Midi::Target target;
		target.kind = Midi::TargetKind::Hotkey;
		target.hotkey = action.ref;
		choices.push_back({target, action.description, action.partnerDescription});
		categorized.emplace_back(StreamDeck::CategoryName(action.category), choices.size() - 1);
	}

	/* The current target keeps its own entry if it is a setting of a
	 * source or filter, which the list does not offer. */
	if (current.kind == Midi::TargetKind::Property) {
		add(QTStr("Accessibility.Midi.Category.Settings"), current);
	}

	/* Categories keep the order they were added in. */
	std::vector<std::pair<QString, QTreeWidgetItem *>> categories;
	QTreeWidgetItem *currentItem = nullptr;
	for (const auto &[category, index] : categorized) {
		auto found = std::find_if(categories.begin(), categories.end(),
					  [&category](const auto &entry) { return entry.first == category; });
		QTreeWidgetItem *parentItem = found != categories.end() ? found->second : nullptr;
		if (!parentItem) {
			parentItem = new QTreeWidgetItem(tree, {category});
			parentItem->setData(0, Qt::UserRole, -1);
			categories.emplace_back(category, parentItem);
		}

		const Choice &choice = choices[index];
		QTreeWidgetItem *item = new QTreeWidgetItem(parentItem, {choice.description});
		item->setData(0, Qt::UserRole, static_cast<int>(index));
		if (current.IsValid() && choice.target.SameAs(current)) {
			currentItem = item;
		}
	}
	tree->expandAll();
	KeepTreeExpansionAccessible(tree);

	connect(search, &QLineEdit::textChanged, this, &MidiTargetPicker::Filter);
	connect(tree, &QTreeWidget::currentItemChanged, this, [this]() {
		const Choice *choice = CurrentChoice();
		toggle->setChecked(choice && !choice->partnerDescription.isEmpty());
		UpdateToggle();
	});
	connect(tree, &QTreeWidget::itemActivated, this, [this]() {
		if (CurrentChoice()) {
			accept();
		}
	});
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	if (currentItem) {
		tree->setCurrentItem(currentItem);
		toggle->setChecked(current.kind == Midi::TargetKind::Hotkey && current.hotkey.toggle &&
				   toggle->isEnabled());
	} else if (tree->topLevelItemCount() > 0) {
		QTreeWidgetItem *first = tree->topLevelItem(0);
		tree->setCurrentItem(first->childCount() > 0 ? first->child(0) : first);
	}
	UpdateToggle();

	tree->setFocus();
	resize(560, 500);
}

const MidiTargetPicker::Choice *MidiTargetPicker::CurrentChoice() const
{
	QTreeWidgetItem *item = tree->currentItem();
	int index = item ? item->data(0, Qt::UserRole).toInt() : -1;
	if (index < 0 || index >= static_cast<int>(choices.size())) {
		return nullptr;
	}
	return &choices[static_cast<size_t>(index)];
}

Midi::Target MidiTargetPicker::SelectedTarget() const
{
	const Choice *choice = CurrentChoice();
	if (!choice) {
		return {};
	}

	Midi::Target target = choice->target;
	if (target.kind == Midi::TargetKind::Hotkey) {
		target.hotkey.toggle = toggle->isEnabled() && toggle->isChecked();
	}
	return target;
}

void MidiTargetPicker::Filter(const QString &text)
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

void MidiTargetPicker::UpdateToggle()
{
	const Choice *choice = CurrentChoice();
	bool paired = choice && !choice->partnerDescription.isEmpty();

	toggle->setEnabled(paired);
	toggle->setText(paired ? QTStr("Accessibility.StreamDeck.Picker.Toggle").arg(choice->partnerDescription)
			       : QTStr("Accessibility.StreamDeck.Picker.ToggleUnavailable"));
	okButton->setEnabled(choice != nullptr);
}

MidiOptionsDialog::MidiOptionsDialog(const Midi::Assignment &assignment, QWidget *parent)
	: QDialog(parent),
	  options(assignment.options)
{
	setWindowTitle(QTStr("Accessibility.Midi.OptionsTitle").arg(Midi::DescribeControl(assignment.control)));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	QFormLayout *form = new QFormLayout;
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(Midi::DescribeTarget(assignment.target), this));
	layout->addLayout(form);

	bool continuous = assignment.target.IsContinuous();
	bool cc = assignment.control.type == Midi::ControlType::ControlChange;

	if (continuous && cc) {
		encoder = new QComboBox(this);
		encoder->addItem(QTStr("Accessibility.Midi.Encoder.Absolute"),
				 static_cast<int>(Midi::EncoderMode::Absolute));
		encoder->addItem(QTStr("Accessibility.Midi.Encoder.TwosComplement"),
				 static_cast<int>(Midi::EncoderMode::TwosComplement));
		encoder->addItem(QTStr("Accessibility.Midi.Encoder.BinaryOffset"),
				 static_cast<int>(Midi::EncoderMode::BinaryOffset));
		encoder->addItem(QTStr("Accessibility.Midi.Encoder.SignMagnitude"),
				 static_cast<int>(Midi::EncoderMode::SignMagnitude));
		encoder->setCurrentIndex(std::max(encoder->findData(static_cast<int>(options.encoder)), 0));
		form->addRow(QTStr("Accessibility.Midi.Encoder"), encoder);
	}

	if (continuous) {
		takeover = new QCheckBox(QTStr("Accessibility.Midi.Takeover"), this);
		takeover->setChecked(options.takeover);
		layout->addWidget(takeover);

		invert = new QCheckBox(QTStr("Accessibility.Midi.Invert"), this);
		invert->setChecked(options.invert);
		layout->addWidget(invert);

		QFormLayout *rangeForm = new QFormLayout;
		low = new QSpinBox(this);
		high = new QSpinBox(this);
		for (QSpinBox *spin : {low, high}) {
			spin->setRange(0, 100);
			spin->setSuffix(" %");
		}
		low->setValue(static_cast<int>(std::lround(options.low * 100.0)));
		high->setValue(static_cast<int>(std::lround(options.high * 100.0)));
		rangeForm->addRow(QTStr("Accessibility.Midi.RangeLow"), low);
		rangeForm->addRow(QTStr("Accessibility.Midi.RangeHigh"), high);
		layout->addLayout(rangeForm);

		if (encoder) {
			auto update = [this]() {
				takeover->setEnabled(encoder->currentData().toInt() ==
						     static_cast<int>(Midi::EncoderMode::Absolute));
			};
			connect(encoder, &QComboBox::currentIndexChanged, this, update);
			update();
		}
	}

	feedback = new QCheckBox(QTStr("Accessibility.Midi.Feedback"), this);
	feedback->setChecked(options.feedback);
	layout->addWidget(feedback);

	if (!continuous) {
		QFormLayout *valuesForm = new QFormLayout;
		onValue = new QSpinBox(this);
		offValue = new QSpinBox(this);
		for (QSpinBox *spin : {onValue, offValue}) {
			spin->setRange(0, 127);
		}
		onValue->setValue(options.onValue);
		offValue->setValue(options.offValue);
		valuesForm->addRow(QTStr("Accessibility.Midi.OnValue"), onValue);
		valuesForm->addRow(QTStr("Accessibility.Midi.OffValue"), offValue);
		layout->addLayout(valuesForm);

		auto update = [this]() {
			onValue->setEnabled(feedback->isChecked());
			offValue->setEnabled(feedback->isChecked());
		};
		connect(feedback, &QCheckBox::toggled, this, update);
		update();
	}

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

Midi::Options MidiOptionsDialog::SelectedOptions() const
{
	Midi::Options selected = options;
	if (encoder) {
		selected.encoder = static_cast<Midi::EncoderMode>(encoder->currentData().toInt());
	}
	if (takeover) {
		selected.takeover = takeover->isChecked();
	}
	if (invert) {
		selected.invert = invert->isChecked();
	}
	if (low && high) {
		selected.low = low->value() / 100.0;
		selected.high = high->value() / 100.0;
	}
	if (feedback) {
		selected.feedback = feedback->isChecked();
	}
	if (onValue && offValue) {
		selected.onValue = onValue->value();
		selected.offValue = offValue->value();
	}
	return selected;
}

} // namespace OBS::Accessibility
