#pragma once

#include <obs-frontend-api.h>

#include <QGroupBox>

class OBSBasic;
class QAction;
class QMenu;
class QPushButton;

namespace OBS::Accessibility {

/* Output and mode controls. Each button triggers an action that is also in
 * the window's Controls menu, and its text reflects the current state. */
class ControlsPanel : public QGroupBox {
	Q_OBJECT

public:
	ControlsPanel(OBSBasic *main, QWidget *parent = nullptr);
	~ControlsPanel();

	QWidget *FirstControl() const;
	void FillMenu(QMenu *menu) const;

private:
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);

	QPushButton *AddButton(QAction *action);
	void UpdateState();

	OBSBasic *main;

	QAction *streamAction;
	QAction *recordAction;
	QAction *pauseAction;
	QAction *replayBufferAction;
	QAction *saveReplayAction;
	QAction *virtualCamAction;
	QAction *studioModeAction;

	QPushButton *streamButton;
};

} // namespace OBS::Accessibility
