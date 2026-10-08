#pragma once

#include "AccessibleFormat.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

class OBSBasic;
class QTimer;

namespace OBS::Accessibility {

/* Follows the state that the status bar and the Stats window show (output
 * times, bitrate, dropped and lagged frames, CPU, disk space) and describes it
 * in sentences for the Status panel and the status hotkeys. Warns when frames
 * are lost or the disk fills up, in both interfaces. */
class StatusMonitor : public QObject {
	Q_OBJECT

public:
	explicit StatusMonitor(OBSBasic *main);
	~StatusMonitor();

	static StatusMonitor *Get();

	QString StreamingText() const;
	QString StreamHealthText() const;
	QString RecordingText() const;
	QString ReplayBufferText() const;
	QString VirtualCamText() const;
	QString PerformanceText() const;
	QString LagText() const;
	QString DiskSpaceText() const;
	QString LastMessageText() const;

	/* The lines of the Status panel, in order. Lines for outputs that are
	 * not available are left out. */
	QStringList Lines() const;

	/* Streaming and recording state with their times, for a quick check. */
	QString SummaryText() const;

signals:
	void Updated();

private:
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	void HandleFrontendEvent(enum obs_frontend_event event);
	void Tick();
	void UpdateBitrate();
	void CheckFrameLoss();
	void CheckDiskSpace();
	void WarnOnce(const char *lookupKey, double ratio, QElapsedTimer &lastWarning);

	OBSBasic *main;
	QTimer *timer;

	int64_t streamSeconds = 0;
	int64_t recordSeconds = 0;

	uint64_t lastBytesSent = 0;
	uint64_t lastBytesTime = 0;
	double kbitsPerSec = 0.0;

	FrameLossWindow droppedWindow;
	FrameLossWindow laggedWindow;
	FrameLossWindow skippedWindow;
	QElapsedTimer lastDroppedWarning;
	QElapsedTimer lastLaggedWarning;
	QElapsedTimer lastSkippedWarning;

	bool lowDiskWarned = false;
	bool criticalDiskWarned = false;

	QString lastMessage;
};

} // namespace OBS::Accessibility
