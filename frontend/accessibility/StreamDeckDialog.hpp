#pragma once

#include "StreamDeckActions.hpp"
#include "StreamDeckManager.hpp"
#include "StreamDeckMapping.hpp"

#include <QDialog>
#include <QPointer>

#include <vector>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace OBS::Accessibility {

/* Assigns OBS actions to the keys of the Stream Deck. While it is open, the
 * keys run nothing: pressing one selects it in the list, so that the user can
 * tell which key is which without seeing them. Changes are kept only with
 * OK. */
class StreamDeckDialog : public QDialog {
	Q_OBJECT

public:
	StreamDeckDialog(StreamDeckManager *manager, QWidget *parent);
	~StreamDeckDialog() override;

	/* Shows the dialog, or brings it to the front if it is already open. */
	static void Open(QWidget *parent);

private:
	void UpdateStatus();
	void Refresh();
	QString RowText(int key) const;
	int CurrentKey() const;
	void UpdateButtons();
	void HandlePhysicalKey(int key);
	void Assign();
	void Remove();
	void Try();
	void Save();

	QPointer<StreamDeckManager> manager;
	StreamDeck::Mapping mapping;
	bool choosingAction = false;

	QLineEdit *status;
	QCheckBox *enabled;
	QListWidget *keys;
	QLineEdit *label;
	QPushButton *assignButton;
	QPushButton *removeButton;
	QPushButton *tryButton;
	QCheckBox *announceActions;
	QSpinBox *brightness;
};

/* Lists every OBS hotkey by category, with a search field, to choose the one
 * a key runs. */
class StreamDeckActionPicker : public QDialog {
	Q_OBJECT

public:
	StreamDeckActionPicker(const QString &keyName, const StreamDeck::HotkeyRef &current, QWidget *parent);

	StreamDeck::HotkeyRef SelectedAction() const;
	QString SelectedDescription() const;

private:
	const StreamDeck::ActionInfo *CurrentAction() const;
	void Filter(const QString &text);
	void UpdateToggle();

	std::vector<StreamDeck::ActionInfo> actions;

	QLineEdit *search;
	QTreeWidget *tree;
	QCheckBox *toggle;
	QPushButton *okButton;
};

} // namespace OBS::Accessibility
