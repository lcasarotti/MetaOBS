#pragma once

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QObject>

namespace OBS::Accessibility {

/* Announces output state changes (streaming, recording, replay buffer,
 * virtual camera), output errors and status bar messages. */
class OutputAnnouncements : public QObject {
	Q_OBJECT

public:
	explicit OutputAnnouncements(QObject *parent = nullptr);
	~OutputAnnouncements();

	void ConnectStatusBar(QObject *statusBar);

private:
	enum class OutputKind {
		Streaming,
		Recording,
		ReplayBuffer,
	};

	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	static void OnStreamingStop(void *data, calldata_t *params);
	static void OnRecordingStop(void *data, calldata_t *params);
	static void OnReplayBufferStop(void *data, calldata_t *params);
	static void OnStreamingReconnect(void *data, calldata_t *params);
	static void OnStreamingReconnectSuccess(void *data, calldata_t *params);

	void HandleFrontendEvent(enum obs_frontend_event event);
	void WatchOutput(OutputKind kind, obs_output_t *output);
	void OnStatusMessage(const QString &message);
	bool IsReconnectMessage(const QString &message) const;

	static QString DescribeStopError(OutputKind kind, int code, const char *lastError);

	OBSSignal streamingStopSignal;
	OBSSignal recordingStopSignal;
	OBSSignal replayBufferStopSignal;
	OBSSignal streamingReconnectSignal;
	OBSSignal streamingReconnectSuccessSignal;
};

} // namespace OBS::Accessibility
