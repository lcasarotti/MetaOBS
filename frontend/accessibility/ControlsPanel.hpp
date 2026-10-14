#pragma once

#include <obs-frontend-api.h>

#include <QGroupBox>

class OBSBasic;
class QAction;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

namespace OBS::Accessibility {

/* Output, mode and transition controls. Each button triggers an action that
 * is also in the window's Controls menu, and its text reflects the current
 * state. In Studio Mode the panel also holds the Transition button and the
 * T-bar. */
class ControlsPanel : public QGroupBox {
	Q_OBJECT

public:
	ControlsPanel(OBSBasic *main, QWidget *parent = nullptr);
	~ControlsPanel();

	QWidget *FirstControl() const;
	QWidget *LastControl() const;
	void FillMenu(QMenu *menu);

private:
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);

	QPushButton *AddButton(QHBoxLayout *layout, QAction *action);
	void CreateTransitionControls(QHBoxLayout *layout);
	void UpdateState();
	void ScheduleTransitionsRefresh();
	void RefreshTransitions();
	void UpdateTBar();
	void MoveTBar(int percent);

	void FillQuickTransitionsMenu();
	void FillTransitionsMenu();

	OBSBasic *main;

	QAction *streamAction;
	QAction *recordAction;
	QAction *pauseAction;
	QAction *replayBufferAction;
	QAction *saveReplayAction;
	QAction *virtualCamAction;
	QAction *studioModeAction;
	QAction *transitionAction;

	QPushButton *streamButton;

	QComboBox *transitionList;
	QLabel *durationLabel;
	QSpinBox *duration;
	QLabel *tBarLabel;
	QSlider *tBar;
	QTimer *transitionsTimer;
	bool updatingTransitions = false;

	QMenu *quickTransitionsMenu = nullptr;
	QMenu *transitionsMenu = nullptr;
};

} // namespace OBS::Accessibility
