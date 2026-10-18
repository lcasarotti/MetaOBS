#pragma once

#include "MidiDevices.hpp"
#include "MidiMapping.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

class QTimer;

namespace OBS::Accessibility {

/* Listens to MIDI controllers and runs what each control is assigned to:
 * hotkeys, volumes, the T-bar, settings of sources and filters. It opens every
 * input port the user has not turned off, looks for new ones every few
 * seconds, and sends the state of the targets back to the controllers, for
 * their lights and motorized faders. Windows only. */
class MidiManager : public QObject {
	Q_OBJECT

public:
	explicit MidiManager(QObject *parent);
	~MidiManager() override;

	static MidiManager *Get();

	bool IsEnabled() const;
	void SetEnabled(bool enabled);

	/* Whether moving a control speaks the new value, and pressing one what
	 * it did. */
	bool GetAnnounceValues() const;
	void SetAnnounceValues(bool announce);

	const Midi::Mapping &GetMapping() const { return mapping; }
	void SetMapping(const Midi::Mapping &newMapping);

	const Midi::PortSettings &GetPortSettings() const { return portSettings; }
	void SetPortSettings(const Midi::PortSettings &settings);

	/* The ports present now. */
	const std::vector<QString> &InputNames() const { return inputNames; }
	const std::vector<QString> &OutputNames() const { return outputNames; }
	bool IsInputOpen(const QString &input) const;
	/* Input ports that could not be opened, usually because another
	 * program uses them. */
	QStringList FailedInputs() const { return failedInputs; }
	/* The output port an input port's feedback goes to, or empty. */
	QString OutputFor(const QString &input) const;

	/* While captured, MIDI messages are only reported through
	 * MessageReceived and run nothing: MIDI learn and the setup dialog
	 * use them to tell which control is which. Calls nest. */
	void Capture();
	void Release();

signals:
	/* Every message received, on the UI thread. */
	void MessageReceived(const QString &port, const Midi::Message &message);
	void PortsChanged();
	void MappingChanged();

private:
	struct Incoming {
		QString port;
		uint32_t message;
	};

	void Scan(bool announce);
	void OpenPorts();
	void ClosePorts();
	void Enqueue(const QString &port, uint32_t message);
	void ProcessQueue();
	void HandleButton(const Midi::Assignment &assignment, bool pressed);
	void HandleAbsolute(const Midi::Assignment &assignment, double value);
	void HandleRelative(const Midi::Assignment &assignment, int steps);
	void MoveTBar(double value);
	void ReleaseHeldHotkeys();
	void SaveConfig();

	void AnnounceNow(const QString &text);
	void ScheduleValueAnnouncement(const Midi::Target &target);
	void AnnounceTakeover(const Midi::Target &target, bool up);
	void AnnounceUnavailable(const Midi::Target &target);

	void SendFeedback();
	void ClearFeedback();

	static void OnFrontendEvent(enum obs_frontend_event event, void *data);

	QTimer *scanTimer = nullptr;
	QTimer *feedbackTimer = nullptr;
	QTimer *announceTimer = nullptr;
	bool exiting = false;

	std::vector<QString> inputNames;
	std::vector<QString> outputNames;
	std::vector<std::unique_ptr<Midi::InputPort>> inputs;
	std::map<QString, std::unique_ptr<Midi::OutputPort>> outputs;
	QStringList failedInputs;

	/* Messages wait here until the UI thread handles them in a batch. */
	std::mutex queueMutex;
	std::vector<Incoming> queue;
	bool processingPosted = false;

	Midi::Mapping mapping;
	/* Changes whenever the mapping does, to notice a change in the middle
	 * of a batch. */
	int mappingGeneration = 0;
	Midi::PortSettings portSettings;
	int captureCount = 0;

	/* Whether each key or button is down, to act once per press. */
	std::map<Midi::Control, bool> pressed;
	/* The hotkeys pressed by controls that are still held, to release the
	 * same ones even if the assignment changes meanwhile. */
	std::map<Midi::Control, obs_hotkey_id> heldHotkeys;
	std::map<Midi::Control, Midi::Takeover> takeovers;
	/* When each control was last moved, so its own movement is not sent
	 * back to it. */
	std::map<Midi::Control, QElapsedTimer> lastInput;
	/* What each control was last sent, to send only changes. */
	std::map<Midi::Control, int> lastSent;

	/* The T-bar goes back to the start after each transition, but a fader
	 * stays at the end: the next transition runs the fader the other
	 * way. */
	bool tBarReversed = false;
	bool tBarMoving = false;

	Midi::Target pendingAnnouncement;
	QElapsedTimer lastTakeoverAnnouncement;
	QElapsedTimer lastUnavailableAnnouncement;
};

} // namespace OBS::Accessibility
