#include "ControlsPanel.hpp"
#include "AccessibleBridge.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>

#include "moc_ControlsPanel.cpp"

namespace OBS::Accessibility {

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

	QHBoxLayout *layout = new QHBoxLayout(this);
	streamButton = AddButton(streamAction);
	AddButton(recordAction);
	AddButton(pauseAction);
	AddButton(replayBufferAction);
	AddButton(saveReplayAction);
	AddButton(virtualCamAction);
	AddButton(studioModeAction);
	layout->addStretch();

	obs_frontend_add_event_callback(OnFrontendEvent, this);

	UpdateState();
}

ControlsPanel::~ControlsPanel()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
}

QPushButton *ControlsPanel::AddButton(QAction *action)
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

	layout()->addWidget(button);
	return button;
}

QWidget *ControlsPanel::FirstControl() const
{
	return streamButton;
}

void ControlsPanel::FillMenu(QMenu *menu) const
{
	menu->addAction(streamAction);
	menu->addAction(recordAction);
	menu->addAction(pauseAction);
	menu->addAction(replayBufferAction);
	menu->addAction(saveReplayAction);
	menu->addAction(virtualCamAction);
	menu->addSeparator();
	menu->addAction(studioModeAction);
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

	studioModeAction->setChecked(obs_frontend_preview_program_mode_active());
}

} // namespace OBS::Accessibility
