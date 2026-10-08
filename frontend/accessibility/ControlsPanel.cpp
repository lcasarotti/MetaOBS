#include "ControlsPanel.hpp"
#include "AccessibleBridge.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "moc_ControlsPanel.cpp"

namespace OBS::Accessibility {

namespace {
/* The same limits as the main window's duration box. */
constexpr int minimumDurationMs = 50;
constexpr int maximumDurationMs = 20000;
constexpr int durationStepMs = 50;
constexpr int tBarSteps = 100;
} // namespace

ControlsPanel::ControlsPanel(OBSBasic *main_, QWidget *parent)
	: QGroupBox(QTStr("Basic.Main.Controls"), parent),
	  main(main_)
{
	streamAction = new QAction(this);
	recordAction = new QAction(this);
	pauseAction = new QAction(this);
	replayBufferAction = new QAction(this);
	saveReplayAction = new QAction(QTStr("Basic.Main.SaveReplay"), this);
	virtualCamAction = new QAction(this);
	studioModeAction = new QAction(QTStr("Basic.TogglePreviewProgramMode"), this);
	studioModeAction->setCheckable(true);
	transitionAction = new QAction(QTStr("Transition"), this);

	connect(streamAction, &QAction::triggered, this, [this]() { OBSAccessibleBridge::StreamAction(main); });
	connect(recordAction, &QAction::triggered, this, [this]() { OBSAccessibleBridge::RecordAction(main); });
	connect(pauseAction, &QAction::triggered, this, [this]() { OBSAccessibleBridge::RecordPauseToggle(main); });
	connect(replayBufferAction, &QAction::triggered, this,
		[this]() { OBSAccessibleBridge::ReplayBufferAction(main); });
	connect(saveReplayAction, &QAction::triggered, this, [this]() { main->ReplayBufferSave(); });
	connect(virtualCamAction, &QAction::triggered, this,
		[this]() { OBSAccessibleBridge::VirtualCamAction(main); });
	connect(studioModeAction, &QAction::triggered, this, [this]() {
		OBSAccessibleBridge::ToggleStudioMode(main);
		UpdateState();
	});
	connect(transitionAction, &QAction::triggered, this,
		[]() { obs_frontend_preview_program_trigger_transition(); });

	QVBoxLayout *layout = new QVBoxLayout(this);
	QHBoxLayout *outputsLayout = new QHBoxLayout();
	QHBoxLayout *transitionsLayout = new QHBoxLayout();
	layout->addLayout(outputsLayout);
	layout->addLayout(transitionsLayout);

	streamButton = AddButton(outputsLayout, streamAction);
	AddButton(outputsLayout, recordAction);
	AddButton(outputsLayout, pauseAction);
	AddButton(outputsLayout, replayBufferAction);
	AddButton(outputsLayout, saveReplayAction);
	AddButton(outputsLayout, virtualCamAction);
	AddButton(outputsLayout, studioModeAction);
	outputsLayout->addStretch();

	CreateTransitionControls(transitionsLayout);

	/* Transitions are added, renamed and removed through several paths,
	 * so the list is rebuilt once after any of them. */
	transitionsTimer = new QTimer(this);
	transitionsTimer->setSingleShot(true);
	transitionsTimer->setInterval(0);
	connect(transitionsTimer, &QTimer::timeout, this, &ControlsPanel::RefreshTransitions);
	connect(main, &OBSBasic::TransitionAdded, this, &ControlsPanel::ScheduleTransitionsRefresh);
	connect(main, &OBSBasic::TransitionRenamed, this, &ControlsPanel::ScheduleTransitionsRefresh);
	connect(main, &OBSBasic::TransitionRemoved, this, &ControlsPanel::ScheduleTransitionsRefresh);
	connect(main, &OBSBasic::TransitionsCleared, this, &ControlsPanel::ScheduleTransitionsRefresh);
	connect(main, &OBSBasic::CurrentTransitionChanged, this, &ControlsPanel::ScheduleTransitionsRefresh);
	connect(main, &OBSBasic::TransitionDurationChanged, this, &ControlsPanel::ScheduleTransitionsRefresh);

	obs_frontend_add_event_callback(OnFrontendEvent, this);

	UpdateState();
	RefreshTransitions();
}

ControlsPanel::~ControlsPanel()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
}

QPushButton *ControlsPanel::AddButton(QHBoxLayout *layout, QAction *action)
{
	QPushButton *button = new QPushButton(this);
	button->setCheckable(action->isCheckable());
	connect(button, &QPushButton::clicked, action, &QAction::trigger);

	auto update = [button, action]() {
		button->setText(action->text());
		button->setEnabled(action->isEnabled());
		button->setVisible(action->isVisible());
		button->setChecked(action->isChecked());
	};
	connect(action, &QAction::changed, button, update);
	update();

	layout->addWidget(button);
	return button;
}

void ControlsPanel::CreateTransitionControls(QHBoxLayout *layout)
{
	QLabel *transitionLabel = new QLabel(QTStr("Accessibility.Controls.CurrentTransition"), this);
	transitionList = new QComboBox(this);
	transitionLabel->setBuddy(transitionList);

	durationLabel = new QLabel(QTStr("Basic.TransitionDuration"), this);
	duration = new QSpinBox(this);
	duration->setRange(minimumDurationMs, maximumDurationMs);
	duration->setSingleStep(durationStepMs);
	duration->setSuffix(" ms");
	durationLabel->setBuddy(duration);

	QPushButton *transitionButton = AddButton(layout, transitionAction);

	/* The T-bar moves in percent; the main window's slider has a finer
	 * scale. */
	tBarLabel = new QLabel(QTStr("Accessibility.Controls.TBar"), this);
	tBar = new QSlider(Qt::Horizontal, this);
	tBar->setRange(0, tBarSteps);
	tBar->setSingleStep(1);
	tBar->setPageStep(10);
	tBarLabel->setBuddy(tBar);
	tBar->setAccessibleName(tBarLabel->text());

	/* AddButton() appended the Transition button; keep it after the
	 * transition and its duration. */
	layout->removeWidget(transitionButton);
	layout->addWidget(transitionLabel);
	layout->addWidget(transitionList, 1);
	layout->addWidget(durationLabel);
	layout->addWidget(duration);
	layout->addWidget(transitionButton);
	layout->addWidget(tBarLabel);
	layout->addWidget(tBar, 1);

	connect(transitionList, &QComboBox::activated, this, [this](int index) {
		QString uuid = transitionList->itemData(index).toString();
		for (const OBSSource &candidate : OBSAccessibleBridge::Transitions(main)) {
			if (QT_UTF8(obs_source_get_uuid(candidate)) == uuid) {
				obs_frontend_set_current_transition(candidate);
				return;
			}
		}
	});
	connect(duration, &QSpinBox::valueChanged, this, [this](int value) {
		if (!updatingTransitions) {
			obs_frontend_set_transition_duration(value);
		}
	});
	connect(tBar, &QSlider::valueChanged, this, &ControlsPanel::MoveTBar);
}

QWidget *ControlsPanel::FirstControl() const
{
	return streamButton;
}

void ControlsPanel::FillMenu(QMenu *menu)
{
	menu->addAction(streamAction);
	menu->addAction(recordAction);
	menu->addAction(pauseAction);
	menu->addAction(replayBufferAction);
	menu->addAction(saveReplayAction);
	menu->addAction(virtualCamAction);
	menu->addSeparator();
	menu->addAction(studioModeAction);
	menu->addAction(transitionAction);

	quickTransitionsMenu = menu->addMenu(QTStr("QuickTransitions"));
	transitionsMenu = menu->addMenu(QTStr("Basic.SceneTransitions"));

	/* The quick transitions and the transitions change with the scene
	 * collection, so the submenus are built when they open. */
	connect(quickTransitionsMenu, &QMenu::aboutToShow, this, &ControlsPanel::FillQuickTransitionsMenu);
	connect(transitionsMenu, &QMenu::aboutToShow, this, &ControlsPanel::FillTransitionsMenu);

	UpdateState();
}

void ControlsPanel::FillQuickTransitionsMenu()
{
	quickTransitionsMenu->clear();
	for (const auto &quickTransition : OBSAccessibleBridge::QuickTransitions(main)) {
		QAction *action = quickTransitionsMenu->addAction(quickTransition.text);
		int id = quickTransition.id;
		connect(action, &QAction::triggered, this,
			[this, id]() { OBSAccessibleBridge::TriggerQuickTransition(main, id); });
	}
}

void ControlsPanel::FillTransitionsMenu()
{
	transitionsMenu->clear();

	OBSSourceAutoRelease current = obs_frontend_get_current_transition();
	QActionGroup *group = new QActionGroup(transitionsMenu);
	for (const OBSSource &transition : OBSAccessibleBridge::Transitions(main)) {
		QAction *action = transitionsMenu->addAction(QT_UTF8(obs_source_get_name(transition)));
		action->setCheckable(true);
		action->setChecked(transition.Get() == current.Get());
		group->addAction(action);

		OBSWeakSource weak = OBSGetWeakRef(transition);
		connect(action, &QAction::triggered, this, [weak]() {
			OBSSource source = OBSGetStrongRef(weak);
			if (source) {
				obs_frontend_set_current_transition(source);
			}
		});
	}
	transitionsMenu->addSeparator();

	QMenu *addMenu = transitionsMenu->addMenu(QTStr("Accessibility.Controls.AddTransition"));
	size_t index = 0;
	const char *id = nullptr;
	while (obs_enum_transition_types(index++, &id)) {
		if (!obs_is_source_configurable(id)) {
			continue;
		}
		QAction *action = addMenu->addAction(QT_UTF8(obs_source_get_display_name(id)));
		std::string typeId = id;
		connect(action, &QAction::triggered, this,
			[this, typeId]() { OBSAccessibleBridge::AddTransition(main, typeId.c_str()); });
	}
	addMenu->setEnabled(!addMenu->isEmpty());

	/* Cut and Fade cannot be renamed, configured or removed. */
	bool configurable = current && obs_source_configurable(current);

	QAction *rename = transitionsMenu->addAction(QTStr("Rename"));
	rename->setEnabled(configurable);
	connect(rename, &QAction::triggered, this, [this]() { OBSAccessibleBridge::RenameCurrentTransition(main); });

	QAction *properties = transitionsMenu->addAction(QTStr("Properties"));
	properties->setEnabled(configurable);
	connect(properties, &QAction::triggered, this, [this]() {
		OBSSourceAutoRelease transition = obs_frontend_get_current_transition();
		if (transition && obs_source_configurable(transition)) {
			main->CreatePropertiesWindow(transition);
		}
	});

	QAction *remove = transitionsMenu->addAction(QTStr("Remove"));
	remove->setEnabled(configurable);
	connect(remove, &QAction::triggered, this, [this]() { OBSAccessibleBridge::RemoveCurrentTransition(main); });
}

void ControlsPanel::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto panel = static_cast<ControlsPanel *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_STARTING:
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		panel->UpdateState();
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		panel->ScheduleTransitionsRefresh();
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
	case OBS_FRONTEND_EVENT_TBAR_VALUE_CHANGED:
		panel->UpdateTBar();
		break;
	default:
		break;
	}
}

void ControlsPanel::UpdateState()
{
	bool streaming = obs_frontend_streaming_active();
	bool recording = obs_frontend_recording_active();
	bool paused = OBSAccessibleBridge::IsRecordingPaused(main);
	bool replayBuffer = obs_frontend_replay_buffer_active();
	bool virtualCam = obs_frontend_virtualcam_active();
	bool studioMode = obs_frontend_preview_program_mode_active();

	streamAction->setText(QTStr(streaming ? "Basic.Main.StopStreaming" : "Basic.Main.StartStreaming"));
	recordAction->setText(QTStr(recording ? "Basic.Main.StopRecording" : "Basic.Main.StartRecording"));

	pauseAction->setText(QTStr(paused ? "Basic.Main.UnpauseRecording" : "Basic.Main.PauseRecording"));
	pauseAction->setEnabled(recording);

	bool replayBufferAvailable = OBSAccessibleBridge::IsReplayBufferAvailable(main);
	replayBufferAction->setText(
		QTStr(replayBuffer ? "Basic.Main.StopReplayBuffer" : "Basic.Main.StartReplayBuffer"));
	replayBufferAction->setVisible(replayBufferAvailable);
	saveReplayAction->setVisible(replayBufferAvailable);
	saveReplayAction->setEnabled(replayBuffer);

	virtualCamAction->setText(QTStr(virtualCam ? "Basic.Main.StopVirtualCam" : "Basic.Main.StartVirtualCam"));
	virtualCamAction->setVisible(OBSAccessibleBridge::IsVirtualCamAvailable(main));

	/* The Transition button, the T-bar and the quick transitions only
	 * work in Studio Mode. */
	studioModeAction->setChecked(studioMode);
	transitionAction->setVisible(studioMode);
	tBarLabel->setVisible(studioMode);
	tBar->setVisible(studioMode);
	if (quickTransitionsMenu) {
		quickTransitionsMenu->menuAction()->setVisible(studioMode);
	}
	UpdateTBar();
}

void ControlsPanel::ScheduleTransitionsRefresh()
{
	transitionsTimer->start();
}

void ControlsPanel::RefreshTransitions()
{
	updatingTransitions = true;

	OBSSourceAutoRelease current = obs_frontend_get_current_transition();
	QSignalBlocker blocker(transitionList);
	transitionList->clear();
	for (const OBSSource &transition : OBSAccessibleBridge::Transitions(main)) {
		transitionList->addItem(QT_UTF8(obs_source_get_name(transition)),
					QT_UTF8(obs_source_get_uuid(transition)));
		if (transition.Get() == current.Get()) {
			transitionList->setCurrentIndex(transitionList->count() - 1);
		}
	}

	/* Cut and other fixed transitions have no duration. */
	bool fixed = current && obs_transition_fixed(current);
	durationLabel->setVisible(!fixed);
	duration->setVisible(!fixed);
	duration->setValue(obs_frontend_get_transition_duration());

	updatingTransitions = false;
}

void ControlsPanel::UpdateTBar()
{
	int maximum = OBSAccessibleBridge::TBarMaximum();
	int position = obs_frontend_preview_program_mode_active() ? obs_frontend_get_tbar_position() : 0;

	QSignalBlocker blocker(tBar);
	tBar->setValue((position * tBarSteps + maximum / 2) / maximum);
}

/* Moving the T-bar from the keyboard has no release, so reaching the end
 * finishes the transition and going back to the start cancels it, as
 * releasing the main window's T-bar near either end does. */
void ControlsPanel::MoveTBar(int percent)
{
	int maximum = OBSAccessibleBridge::TBarMaximum();
	OBSAccessibleBridge::SetTBarPosition(main, percent * maximum / tBarSteps);

	if (percent == 0 || percent == tBarSteps) {
		OBSAccessibleBridge::ReleaseTBar(main);
		QSignalBlocker blocker(tBar);
		tBar->setValue(0);
	}
}

} // namespace OBS::Accessibility
