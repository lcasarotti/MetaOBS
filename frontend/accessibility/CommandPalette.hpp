#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QString>

class QAction;
class QLineEdit;
class QListWidget;
class QMenuBar;
class QTimer;

namespace OBS::Accessibility {

/* Searches the commands of a menu bar by name and runs the chosen one, so
 * commands can be found without knowing which menu holds them. The commands
 * are read from the menus each time the palette opens, so they include the
 * menus that plugins add and follow the state of the selected items. */
class CommandPalette : public QDialog {
	Q_OBJECT

public:
	CommandPalette(QMenuBar *menuBar, QWidget *parent);

	/* Leaves the action out of the palette, such as the action that opens
	 * it. */
	static void Exclude(QAction *action);

	/* Opens the palette and runs the chosen command after it closes. */
	static void Run(QMenuBar *menuBar, QWidget *parent);

	QAction *ChosenAction() const { return chosen; }

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	struct Command {
		QPointer<QAction> action;
		QString text;
		QString searchText;
	};

	void Filter();
	void AnnounceCount();
	void Choose();

	QLineEdit *search;
	QListWidget *list;
	QTimer *announceTimer;
	QList<Command> commands;
	QPointer<QAction> chosen;
};

} // namespace OBS::Accessibility
