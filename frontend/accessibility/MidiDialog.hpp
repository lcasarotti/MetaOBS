#pragma once

#include "MidiManager.hpp"
#include "MidiMapping.hpp"
#include "StreamDeckActions.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace OBS::Accessibility {

/* Sets up the MIDI controllers: which ports MetaOBS listens to, and what each
 * control does. While it is open, controls run nothing: moving one selects
 * its row, adding it to the list if it has no assignment yet. Changes are
 * kept only with OK. */
class MidiDialog : public QDialog {
	Q_OBJECT

public:
	MidiDialog(MidiManager *manager, QWidget *parent);
	~MidiDialog() override;

	/* Shows the dialog, or brings it to the front if it is already open. */
	static void Open(QWidget *parent);

private:
	void UpdateStatus();
	void RefreshPorts();
	void UpdateOutputChoice();
	void RefreshAssignments();
	QString RowText(int row) const;
	/* The assignment of a row, or nullptr for a control without one. */
	Midi::Assignment *AssignmentAt(int row);
	Midi::Control ControlAt(int row) const;
	void UpdateButtons();
	void HandleMessage(const QString &port, const Midi::Message &message);
	void Assign();
	void EditOptions();
	void Remove();
	void Save();

	QPointer<MidiManager> manager;
	Midi::Mapping mapping;
	Midi::PortSettings portSettings;
	/* Controls moved while the dialog is open, that have no assignment. */
	std::vector<Midi::Control> unassigned;
	bool choosing = false;
	QElapsedTimer lastRowAnnouncement;

	QLineEdit *status;
	QCheckBox *enabled;
	QListWidget *ports;
	QComboBox *output;
	QListWidget *assignments;
	QPushButton *assignButton;
	QPushButton *optionsButton;
	QPushButton *removeButton;
	QCheckBox *announceValues;
};

/* Lists everything a MIDI control can do, by category, with a search field:
 * volumes and other audio settings, the T-bar and every OBS hotkey. */
class MidiTargetPicker : public QDialog {
	Q_OBJECT

public:
	/* Only targets a key can run are listed when buttonsOnly is set. */
	MidiTargetPicker(const QString &controlName, const Midi::Target &current, bool buttonsOnly, QWidget *parent);

	Midi::Target SelectedTarget() const;

private:
	struct Choice {
		Midi::Target target;
		QString description;
		/* For hotkeys: the other half of the pair, or empty. */
		QString partnerDescription;
	};

	const Choice *CurrentChoice() const;
	void Filter(const QString &text);
	void UpdateToggle();

	std::vector<Choice> choices;

	QLineEdit *search;
	QTreeWidget *tree;
	QCheckBox *toggle;
	QPushButton *okButton;
};

/* How one control acts on its target: the kind of knob, catching up with the
 * value, direction, range and what is sent back to the controller. */
class MidiOptionsDialog : public QDialog {
	Q_OBJECT

public:
	MidiOptionsDialog(const Midi::Assignment &assignment, QWidget *parent);

	Midi::Options SelectedOptions() const;

private:
	Midi::Options options;

	QComboBox *encoder = nullptr;
	QCheckBox *takeover = nullptr;
	QCheckBox *invert = nullptr;
	QSpinBox *low = nullptr;
	QSpinBox *high = nullptr;
	QCheckBox *feedback = nullptr;
	QSpinBox *onValue = nullptr;
	QSpinBox *offValue = nullptr;
};

} // namespace OBS::Accessibility
