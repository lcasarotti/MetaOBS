#include "OutputAnnouncements.hpp"
#include "AccessibleSettings.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>

#include <QRegularExpression>
#include <QStatusBar>

#include "moc_OutputAnnouncements.cpp"

namespace OBS::Accessibility {

namespace {
void AnnounceOutput(const QString &text, Priority priority)
{
	if (!Settings::GetAnnounceOutputs()) {
		return;
	}

	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(text, priority);
	}
}

void AnnounceOutput(const char *lookupKey, Priority priority)
{
	AnnounceOutput(QTStr(lookupKey), priority);
}

int GetStopCode(calldata_t *params)
{
	return static_cast<int>(calldata_int(params, "code"));
}

const char *GetLastError(calldata_t *params)
{
	obs_output_t *output = static_cast<obs_output_t *>(calldata_ptr(params, "output"));
	return output ? obs_output_get_last_error(output) : nullptr;
}
} // namespace

OutputAnnouncements::OutputAnnouncements(QObject *parent) : QObject(parent)
{
	obs_frontend_add_event_callback(OnFrontendEvent, this);
}

OutputAnnouncements::~OutputAnnouncements()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
}

void OutputAnnouncements::ConnectStatusBar(QObject *statusBar)
{
	QStatusBar *bar = qobject_cast<QStatusBar *>(statusBar);
	if (bar) {
		connect(bar, &QStatusBar::messageChanged, this, &OutputAnnouncements::OnStatusMessage);
	}
}

void OutputAnnouncements::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	static_cast<OutputAnnouncements *>(data)->HandleFrontendEvent(event);
}

void OutputAnnouncements::HandleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING: {
		OBSOutputAutoRelease output = obs_frontend_get_streaming_output();
		WatchOutput(OutputKind::Streaming, output);
		AnnounceOutput("Accessibility.Announce.StreamingStarting", Priority::Low);
		break;
	}
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		AnnounceOutput("Accessibility.Announce.StreamingStarted", Priority::High);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		AnnounceOutput("Accessibility.Announce.StreamingStopping", Priority::Low);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		AnnounceOutput("Accessibility.Announce.StreamingStopped", Priority::High);
		break;

	case OBS_FRONTEND_EVENT_RECORDING_STARTING: {
		OBSOutputAutoRelease output = obs_frontend_get_recording_output();
		WatchOutput(OutputKind::Recording, output);
		AnnounceOutput("Accessibility.Announce.RecordingStarting", Priority::Low);
		break;
	}
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		AnnounceOutput("Accessibility.Announce.RecordingStarted", Priority::High);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
		AnnounceOutput("Accessibility.Announce.RecordingStopping", Priority::Low);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		AnnounceOutput("Accessibility.Announce.RecordingStopped", Priority::High);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		AnnounceOutput("Accessibility.Announce.RecordingPaused", Priority::High);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		AnnounceOutput("Accessibility.Announce.RecordingUnpaused", Priority::High);
		break;

	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING: {
		OBSOutputAutoRelease output = obs_frontend_get_replay_buffer_output();
		WatchOutput(OutputKind::ReplayBuffer, output);
		break;
	}
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		AnnounceOutput("Accessibility.Announce.ReplayBufferStarted", Priority::High);
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		AnnounceOutput("Accessibility.Announce.ReplayBufferStopped", Priority::High);
		break;

	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
		AnnounceOutput("Accessibility.Announce.VirtualCamStarted", Priority::High);
		break;
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
		AnnounceOutput("Accessibility.Announce.VirtualCamStopped", Priority::High);
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		streamingStopSignal.Disconnect();
		recordingStopSignal.Disconnect();
		replayBufferStopSignal.Disconnect();
		streamingReconnectSignal.Disconnect();
		streamingReconnectSuccessSignal.Disconnect();
		break;

	default:
		break;
	}
}

void OutputAnnouncements::WatchOutput(OutputKind kind, obs_output_t *output)
{
	OBSSignal *signal = nullptr;
	signal_callback_t callback = nullptr;

	switch (kind) {
	case OutputKind::Streaming:
		signal = &streamingStopSignal;
		callback = OnStreamingStop;
		break;
	case OutputKind::Recording:
		signal = &recordingStopSignal;
		callback = OnRecordingStop;
		break;
	case OutputKind::ReplayBuffer:
		signal = &replayBufferStopSignal;
		callback = OnReplayBufferStop;
		break;
	}

	if (!output) {
		signal->Disconnect();
		return;
	}

	signal_handler_t *handler = obs_output_get_signal_handler(output);
	signal->Connect(handler, "stop", callback, this);

	if (kind == OutputKind::Streaming) {
		streamingReconnectSignal.Connect(handler, "reconnect", OnStreamingReconnect, this);
		streamingReconnectSuccessSignal.Connect(handler, "reconnect_success", OnStreamingReconnectSuccess,
							this);
	}
}

/* The stop callbacks run on the output's thread. */
void OutputAnnouncements::OnStreamingStop(void *, calldata_t *params)
{
	int code = GetStopCode(params);
	if (code != OBS_OUTPUT_SUCCESS) {
		Announcer::AnnounceFromAnyThread(DescribeStopError(OutputKind::Streaming, code, GetLastError(params)),
						 Priority::High);
	}
}

void OutputAnnouncements::OnRecordingStop(void *, calldata_t *params)
{
	int code = GetStopCode(params);
	if (code != OBS_OUTPUT_SUCCESS) {
		Announcer::AnnounceFromAnyThread(DescribeStopError(OutputKind::Recording, code, GetLastError(params)),
						 Priority::High);
	}
}

void OutputAnnouncements::OnReplayBufferStop(void *, calldata_t *params)
{
	int code = GetStopCode(params);
	if (code != OBS_OUTPUT_SUCCESS) {
		Announcer::AnnounceFromAnyThread(
			DescribeStopError(OutputKind::ReplayBuffer, code, GetLastError(params)), Priority::High);
	}
}

/* The reconnect callbacks run on the output's thread. */
void OutputAnnouncements::OnStreamingReconnect(void *data, calldata_t *params)
{
	auto announcements = static_cast<OutputAnnouncements *>(data);
	int seconds = static_cast<int>(calldata_int(params, "timeout_sec"));

	QMetaObject::invokeMethod(announcements, [seconds]() {
		QString text = QTStr("Accessibility.Announce.StreamReconnecting").arg(seconds);
		AnnounceOutput(text, Priority::High);
	});
}

void OutputAnnouncements::OnStreamingReconnectSuccess(void *data, calldata_t *)
{
	auto announcements = static_cast<OutputAnnouncements *>(data);
	QMetaObject::invokeMethod(announcements, []() {
		AnnounceOutput(QTStr("Basic.StatusBar.ReconnectSuccessful"), Priority::High);
	});
}

QString OutputAnnouncements::DescribeStopError(OutputKind kind, int code, const char *lastError)
{
	const char *descriptionKey = nullptr;
	const char *formatKey = nullptr;

	if (kind == OutputKind::Streaming) {
		formatKey = "Accessibility.Announce.StreamingError";

		switch (code) {
		case OBS_OUTPUT_BAD_PATH:
			descriptionKey = "Output.ConnectFail.BadPath";
			break;
		case OBS_OUTPUT_CONNECT_FAILED:
			descriptionKey = "Output.ConnectFail.ConnectFailed";
			break;
		case OBS_OUTPUT_INVALID_STREAM:
			descriptionKey = "Output.ConnectFail.InvalidStream";
			break;
		case OBS_OUTPUT_ENCODE_ERROR:
			descriptionKey = "Output.StreamEncodeError.Msg";
			break;
		case OBS_OUTPUT_HDR_DISABLED:
			descriptionKey = "Output.ConnectFail.HdrDisabled";
			break;
		case OBS_OUTPUT_DISCONNECTED:
			descriptionKey = "Output.ConnectFail.Disconnected";
			break;
		default:
			descriptionKey = "Output.ConnectFail.Error";
			break;
		}
	} else {
		formatKey = kind == OutputKind::Recording ? "Accessibility.Announce.RecordingError"
							  : "Accessibility.Announce.ReplayBufferError";

		switch (code) {
		case OBS_OUTPUT_UNSUPPORTED:
			descriptionKey = "Output.RecordFail.Unsupported";
			break;
		case OBS_OUTPUT_NO_SPACE:
			descriptionKey = "Output.RecordNoSpace.Msg";
			break;
		case OBS_OUTPUT_ENCODE_ERROR:
			descriptionKey = "Output.RecordError.EncodeErrorMsg";
			break;
		default:
			descriptionKey = "Output.RecordError.Msg";
			break;
		}
	}

	QString description = QTStr(descriptionKey);
	if (lastError && *lastError) {
		description += " " + QString::fromUtf8(lastError);
	}

	return QTStr(formatKey).arg(description);
}

void OutputAnnouncements::OnStatusMessage(const QString &message)
{
	if (message.isEmpty() || !Settings::GetAnnounceStatusMessages() || IsReconnectMessage(message)) {
		return;
	}

	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(message, Priority::Normal);
	}
}

/* While the stream reconnects, the status bar shows a countdown that changes
 * every second. The reconnect signals are announced instead. */
bool OutputAnnouncements::IsReconnectMessage(const QString &message) const
{
	for (const char *lookupKey : {"Basic.StatusBar.Reconnecting", "Basic.StatusBar.AttemptingReconnect"}) {
		QStringList parts = QTStr(lookupKey).split(QRegularExpression("%\\d"));
		for (QString &part : parts) {
			part = QRegularExpression::escape(part);
		}

		QRegularExpression pattern("^" + parts.join("\\d+") + "$");
		if (pattern.match(message).hasMatch()) {
			return true;
		}
	}

	return message == QTStr("Basic.StatusBar.ReconnectSuccessful");
}

} // namespace OBS::Accessibility
