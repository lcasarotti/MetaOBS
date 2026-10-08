#include "StatusMonitor.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleSettings.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>
#include <util/platform.h>

#include <QStatusBar>
#include <QTimer>

#include <algorithm>

#include "moc_StatusMonitor.cpp"

namespace OBS::Accessibility {

namespace {
StatusMonitor *instance = nullptr;

constexpr int tickMs = 1000;
constexpr int64_t frameLossWindowMs = 10000;
constexpr qint64 warningCooldownMs = 60000;
/* Below this many frames in the window, a ratio means little. */
constexpr uint64_t minimumWindowFrames = 30;

constexpr uint64_t lowDiskBytes = 5ULL * 1024 * 1024 * 1024;
constexpr uint64_t criticalDiskBytes = 1ULL * 1024 * 1024 * 1024;

QString Percent(double ratio)
{
	return QString::number(ratio * 100.0, 'f', 1);
}

QString FramesWithPercent(uint64_t frames, uint64_t total)
{
	double ratio = total ? static_cast<double>(frames) / static_cast<double>(total) : 0.0;
	return QString::number(frames) + " (" + Percent(ratio) + "%)";
}

bool AnyOutputActive()
{
	return obs_frontend_streaming_active() || obs_frontend_recording_active() ||
	       obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active();
}
} // namespace

StatusMonitor::StatusMonitor(OBSBasic *main_)
	: QObject(main_),
	  main(main_),
	  droppedWindow(frameLossWindowMs),
	  laggedWindow(frameLossWindowMs),
	  skippedWindow(frameLossWindowMs)
{
	instance = this;

	timer = new QTimer(this);
	timer->setInterval(tickMs);
	connect(timer, &QTimer::timeout, this, &StatusMonitor::Tick);
	timer->start();

	if (QStatusBar *statusBar = main->statusBar()) {
		connect(statusBar, &QStatusBar::messageChanged, this, [this](const QString &message) {
			if (!message.isEmpty()) {
				lastMessage = message;
			}
		});
	}

	obs_frontend_add_event_callback(OnFrontendEvent, this);
}

StatusMonitor::~StatusMonitor()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	if (instance == this) {
		instance = nullptr;
	}
}

StatusMonitor *StatusMonitor::Get()
{
	return instance;
}

void StatusMonitor::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	static_cast<StatusMonitor *>(data)->HandleFrontendEvent(event);
}

void StatusMonitor::HandleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		streamSeconds = 0;
		lastBytesSent = 0;
		lastBytesTime = os_gettime_ns();
		kbitsPerSec = 0.0;
		droppedWindow.Clear();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		recordSeconds = 0;
		lowDiskWarned = false;
		criticalDiskWarned = false;
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		timer->stop();
		break;
	default:
		break;
	}

	emit Updated();
}

void StatusMonitor::Tick()
{
	if (obs_frontend_streaming_active()) {
		streamSeconds++;
		UpdateBitrate();
	}

	if (obs_frontend_recording_active() && !obs_frontend_recording_paused()) {
		recordSeconds++;
	}

	CheckFrameLoss();
	CheckDiskSpace();

	emit Updated();
}

void StatusMonitor::UpdateBitrate()
{
	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();
	if (!output) {
		return;
	}

	uint64_t bytesSent = obs_output_get_total_bytes(output);
	uint64_t now = os_gettime_ns();

	if (bytesSent < lastBytesSent) {
		lastBytesSent = 0;
	}

	double seconds = static_cast<double>(now - lastBytesTime) / 1000000000.0;
	if (seconds > 0.0) {
		kbitsPerSec = static_cast<double>(bytesSent - lastBytesSent) * 8.0 / seconds / 1000.0;
	}

	lastBytesSent = bytesSent;
	lastBytesTime = now;
}

void StatusMonitor::CheckFrameLoss()
{
	int64_t now = static_cast<int64_t>(os_gettime_ns() / 1000000);

	OBSOutputAutoRelease streamOutput = obs_frontend_get_streaming_output();
	if (streamOutput && obs_frontend_streaming_active()) {
		droppedWindow.Add(now, static_cast<uint64_t>(std::max(obs_output_get_total_frames(streamOutput), 0)),
				  static_cast<uint64_t>(std::max(obs_output_get_frames_dropped(streamOutput), 0)));
	} else {
		droppedWindow.Clear();
	}

	laggedWindow.Add(now, obs_get_total_frames(), obs_get_lagged_frames());

	video_t *video = obs_get_video();
	skippedWindow.Add(now, video_output_get_total_frames(video), video_output_get_skipped_frames(video));

	if (!Settings::GetAnnounceWarnings() || !AnyOutputActive()) {
		return;
	}

	double threshold = Settings::GetFrameLossThreshold() / 100.0;
	auto check = [&](FrameLossWindow &window, const char *lookupKey, QElapsedTimer &lastWarning) {
		double ratio = window.Ratio(minimumWindowFrames);
		if (window.IsFull() && ratio >= threshold) {
			WarnOnce(lookupKey, ratio, lastWarning);
		}
	};

	check(droppedWindow, "Accessibility.Warning.DroppedFrames", lastDroppedWarning);
	check(laggedWindow, "Accessibility.Warning.LaggedFrames", lastLaggedWarning);
	check(skippedWindow, "Accessibility.Warning.SkippedFrames", lastSkippedWarning);
}

void StatusMonitor::WarnOnce(const char *lookupKey, double ratio, QElapsedTimer &lastWarning)
{
	if (lastWarning.isValid() && lastWarning.elapsed() < warningCooldownMs) {
		return;
	}

	lastWarning.start();
	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(QTStr(lookupKey).arg(Percent(ratio)), Priority::High);
	}
}

void StatusMonitor::CheckDiskSpace()
{
	if (!Settings::GetAnnounceWarnings() ||
	    (!obs_frontend_recording_active() && !obs_frontend_replay_buffer_active())) {
		return;
	}

	const char *path = main->GetCurrentOutputPath();
	uint64_t freeBytes = path && *path ? os_get_free_disk_space(path) : 0;
	if (freeBytes == 0) {
		return;
	}

	bool critical = freeBytes < criticalDiskBytes && !criticalDiskWarned;
	bool low = freeBytes < lowDiskBytes && !lowDiskWarned;
	if (!critical && !low) {
		return;
	}

	lowDiskWarned = true;
	criticalDiskWarned = criticalDiskWarned || critical;

	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(QTStr("Accessibility.Warning.LowDisk").arg(FormatBytes(freeBytes)), Priority::High);
	}
}

QString StatusMonitor::StreamingText() const
{
	if (!obs_frontend_streaming_active()) {
		return QTStr("Accessibility.Status.Streaming.Inactive");
	}

	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();
	bool reconnecting = output && obs_output_reconnecting(output);
	const char *lookupKey = reconnecting ? "Accessibility.Status.Streaming.Reconnecting"
					     : "Accessibility.Status.Streaming.Live";
	return QTStr(lookupKey).arg(FormatDuration(streamSeconds));
}

QString StatusMonitor::StreamHealthText() const
{
	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();
	if (!output || !obs_frontend_streaming_active()) {
		return QTStr("Accessibility.Status.StreamHealth.Inactive");
	}

	uint64_t dropped = static_cast<uint64_t>(std::max(obs_output_get_frames_dropped(output), 0));
	uint64_t total = static_cast<uint64_t>(std::max(obs_output_get_total_frames(output), 0));

	return QTStr("Accessibility.Status.StreamHealth")
		.arg(QString::number(kbitsPerSec, 'f', 0), FramesWithPercent(dropped, total));
}

QString StatusMonitor::RecordingText() const
{
	if (!obs_frontend_recording_active()) {
		return QTStr("Accessibility.Status.Recording.Inactive");
	}

	bool paused = obs_frontend_recording_paused();
	return QTStr(paused ? "Accessibility.Status.Recording.Paused" : "Accessibility.Status.Recording.Active")
		.arg(FormatDuration(recordSeconds));
}

QString StatusMonitor::ReplayBufferText() const
{
	return QTStr(obs_frontend_replay_buffer_active() ? "Accessibility.Status.ReplayBuffer.Active"
							 : "Accessibility.Status.ReplayBuffer.Inactive");
}

QString StatusMonitor::VirtualCamText() const
{
	return QTStr(obs_frontend_virtualcam_active() ? "Accessibility.Status.VirtualCam.Active"
						      : "Accessibility.Status.VirtualCam.Inactive");
}

QString StatusMonitor::PerformanceText() const
{
	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	double targetFps = ovi.fps_den ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 0.0;

	return QTStr("Accessibility.Status.Performance")
		.arg(QString::number(main->GetCPUUsage(), 'f', 1), QString::number(obs_get_active_fps(), 'f', 2),
		     QString::number(targetFps, 'g', 4));
}

QString StatusMonitor::LagText() const
{
	video_t *video = obs_get_video();
	return QTStr("Accessibility.Status.Lag")
		.arg(FramesWithPercent(obs_get_lagged_frames(), obs_get_total_frames()),
		     FramesWithPercent(video_output_get_skipped_frames(video), video_output_get_total_frames(video)));
}

QString StatusMonitor::DiskSpaceText() const
{
	const char *path = main->GetCurrentOutputPath();
	uint64_t freeBytes = path && *path ? os_get_free_disk_space(path) : 0;
	return QTStr("Accessibility.Status.DiskSpace").arg(FormatBytes(freeBytes));
}

QString StatusMonitor::LastMessageText() const
{
	return lastMessage.isEmpty() ? QTStr("Accessibility.Status.NoMessage")
				     : QTStr("Accessibility.Status.LastMessage").arg(lastMessage);
}

QStringList StatusMonitor::Lines() const
{
	QStringList lines = {StreamingText(), StreamHealthText(), RecordingText()};

	if (OBSAccessibleBridge::IsReplayBufferAvailable(main)) {
		lines.append(ReplayBufferText());
	}
	if (OBSAccessibleBridge::IsVirtualCamAvailable(main)) {
		lines.append(VirtualCamText());
	}

	lines.append({PerformanceText(), LagText(), DiskSpaceText(), LastMessageText()});
	return lines;
}

QString StatusMonitor::SummaryText() const
{
	return StreamingText() + ". " + RecordingText();
}

} // namespace OBS::Accessibility
