#pragma once

#include <QGroupBox>

class QListWidget;
class QMenu;

namespace OBS::Accessibility {

class StatusMonitor;

/* The status bar and the main Stats values as a list of sentences. The list
 * updates while it does not have focus, and right before each key press while
 * it has focus, so the screen reader reads current values when moving through
 * it without being interrupted every second. Enter reads the current line
 * again. */
class StatusPanel : public QGroupBox {
	Q_OBJECT

public:
	explicit StatusPanel(QWidget *parent = nullptr);

	QListWidget *View() const { return view; }
	QMenu *Menu() const { return menu; }

private:
	void Refresh();
	void ReadCurrentLine();

	QListWidget *view;
	QMenu *menu;
};

} // namespace OBS::Accessibility
