#pragma once

#include <obs-frontend-api.h>

#include <QMainWindow>
#include <QPointer>

class OBSBasic;
class QMenu;

namespace OBS::Accessibility {

class AudioPanel;
class ControlsPanel;
class ScenesPanel;
class SourcesPanel;
class StatusPanel;

/* Main window of Accessible Mode, built only from standard Qt controls. It
 * replaces OBSBasic on screen while OBSBasic keeps running hidden, so all
 * the application logic stays in one place. */
class AccessibleMainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit AccessibleMainWindow(OBSBasic *main);
	~AccessibleMainWindow();

	void ShowAndActivate();

protected:
	void closeEvent(QCloseEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);

	void CreateMenus();
	void AddProjectorMenus(QMenu *viewMenu);
	void FocusPanel(int index);
	void CyclePanels(int direction);
	void UpdateTitle(const QString &mainTitle);
	void ReturnFocusAfterDialog();

	OBSBasic *main;
	ScenesPanel *scenesPanel;
	SourcesPanel *sourcesPanel;
	AudioPanel *audioPanel;
	ControlsPanel *controlsPanel;
	StatusPanel *statusPanel;

	/* The panels in F6 order, and the widget that takes focus in each. */
	QList<QWidget *> panels;
	QList<QPointer<QWidget>> panelWidgets;
};

} // namespace OBS::Accessibility
