#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>

class OBSBasic;
class QDockWidget;
class QListWidget;
class QPushButton;

namespace OBS::Accessibility {

/* Lists the docks that plugins and custom browser docks add to the main
 * window, which stay out of reach while the main window is hidden, and opens
 * them as floating windows. */
class DocksDialog : public QDialog {
	Q_OBJECT

public:
	DocksDialog(OBSBasic *main, QWidget *parent);

	static void Open(OBSBasic *main, QWidget *parent);

private:
	void Refresh();
	QDockWidget *SelectedDock() const;
	void OpenSelected();
	void HideSelected();

	OBSBasic *main;
	QListWidget *list;
	QPushButton *openButton;
	QPushButton *hideButton;
	QList<QPointer<QDockWidget>> docks;
};

} // namespace OBS::Accessibility
