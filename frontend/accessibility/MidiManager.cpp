#include "MidiManager.hpp"
#include "AccessibleBridge.hpp"
#include "Announcer.hpp"
#include "MidiTargets.hpp"
#include "StreamDeckActions.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>
#include <util/config-file.h>

#include <QTimer>

#include <algorithm>
#include <cmath>

#include "moc_MidiManager.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int scanIntervalMilliseconds = 2000;
/* Often enough for lights and motorized faders to follow changes made in
 * MetaOBS without delay. */
constexpr int feedbackIntervalMilliseconds = 50;
/* Values are spoken once a knob or fader rests. */
constexpr int announceDelayMilliseconds = 250;
/* A control that moved recently is not sent its own value back. */
constexpr int echoMilliseconds = 300;
constexpr int takeoverAnnouncementMilliseconds = 1000;
/* Near the ends of its travel, a fader finishes or cancels the transition. */
constexpr double tBarEnd = 0.99;
constexpr double tBarStart = 0.01;

constexpr const char *section = "Midi";
constexpr const char *enabledKey = "Enabled";
constexpr const char *announceValuesKey = "AnnounceValues";
constexpr const char *assignmentsKey = "Assignments";
constexpr const char *portsKey = "Ports";

MidiManager *instance = nullptr;

config_t *GetConfig()
{
	return App()->GetUserConfig();
}

QString JoinNames(const QStringList &names)
{
	return names.join(QStringLiteral(", "));
}
} // namespace

MidiManager::MidiManager(QObject *parent) : QObject(parent)
{
	instance = this;

	config_t *config = GetConfig();
	config_set_default_bool(config, section, enabledKey, true);
	config_set_default_bool(config, section, announceValuesKey, true);

	const char *assignments = config_get_string(config, section, assignmentsKey);
	mapping = Midi::MappingFromJson(QT_UTF8(assignments ? assignments : ""));
	const char *ports = config_get_string(config, section, portsKey);
	portSettings = Midi::PortSettingsFromJson(QT_UTF8(ports ? ports : ""));

	announceTimer = new QTimer(this);
	announceTimer->setSingleShot(true);
	announceTimer->setInterval(announceDelayMilliseconds);
	connect(announceTimer, &QTimer::timeout, this, [this]() {
		if (pendingAnnouncement.IsValid()) {
			AnnounceNow(Midi::DescribeValue(pendingAnnouncement));
		}
	});

	feedbackTimer = new QTimer(this);
	feedbackTimer->setInterval(feedbackIntervalMilliseconds);
	connect(feedbackTimer, &QTimer::timeout, this, &MidiManager::SendFeedback);

	obs_frontend_add_event_callback(OnFrontendEvent, this);

	scanTimer = new QTimer(this);
	scanTimer->setInterval(scanIntervalMilliseconds);
	connect(scanTimer, &QTimer::timeout, this, [this]() { Scan(true); });

	/* Controllers already plugged in when MetaOBS starts are not
	 * announced. */
	QTimer::singleShot(0, this, [this]() {
		Scan(false);
		scanTimer->start();
	});
}

MidiManager::~MidiManager()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	ReleaseHeldHotkeys();
	ClearFeedback();
	ClosePorts();
	if (instance == this) {
		instance = nullptr;
	}
}

MidiManager *MidiManager::Get()
{
	return instance;
}

bool MidiManager::IsEnabled() const
{
	return config_get_bool(GetConfig(), section, enabledKey);
}

void MidiManager::SetEnabled(bool enabled)
{
	if (enabled == IsEnabled()) {
		return;
	}

	config_set_bool(GetConfig(), section, enabledKey, enabled);
	SaveConfig();

	/* A disabled controller is left free for other programs. */
	if (!enabled) {
		ReleaseHeldHotkeys();
		ClearFeedback();
	}
	OpenPorts();
}

bool MidiManager::GetAnnounceValues() const
{
	return config_get_bool(GetConfig(), section, announceValuesKey);
}

void MidiManager::SetAnnounceValues(bool announce)
{
	config_set_bool(GetConfig(), section, announceValuesKey, announce);
	SaveConfig();
}

void MidiManager::SetMapping(const Midi::Mapping &newMapping)
{
	/* Lights of controls that lost their assignment go off. */
	for (const Midi::Assignment &old : mapping) {
		if (Midi::FindControl(newMapping, old.control) < 0) {
			auto output = outputs.find(OutputFor(old.control.port));
			if (output != outputs.end() && old.control.type == Midi::ControlType::Note) {
				output->second->Send(
					Midi::PackShortMessage(Midi::MessageFor(old.control, old.options.offValue)));
			}
		}
	}

	mapping = newMapping;
	mappingGeneration++;
	takeovers.clear();
	lastSent.clear();

	config_set_string(GetConfig(), section, assignmentsKey, QT_TO_UTF8(Midi::MappingToJson(mapping)));
	SaveConfig();
	emit MappingChanged();
}

void MidiManager::SetPortSettings(const Midi::PortSettings &settings)
{
	portSettings = settings;
	config_set_string(GetConfig(), section, portsKey, QT_TO_UTF8(Midi::PortSettingsToJson(portSettings)));
	SaveConfig();
	OpenPorts();
}

bool MidiManager::IsInputOpen(const QString &input) const
{
	return std::any_of(inputs.begin(), inputs.end(),
			   [&input](const std::unique_ptr<Midi::InputPort> &port) { return port->Name() == input; });
}

QString MidiManager::OutputFor(const QString &input) const
{
	return Midi::OutputFor(portSettings, input, outputNames);
}

void MidiManager::Capture()
{
	captureCount++;
}

void MidiManager::Release()
{
	captureCount = std::max(captureCount - 1, 0);
}

void MidiManager::SaveConfig()
{
	config_save_safe(GetConfig(), "tmp", nullptr);
}

void MidiManager::Scan(bool announce)
{
	if (exiting) {
		return;
	}

	std::vector<QString> newInputs = Midi::InputPortNames();
	std::vector<QString> newOutputs = Midi::OutputPortNames();
	if (newInputs == inputNames && newOutputs == outputNames) {
		return;
	}

	QStringList added;
	QStringList removed;
	for (const QString &name : newInputs) {
		if (std::find(inputNames.begin(), inputNames.end(), name) == inputNames.end()) {
			added << name;
		}
	}
	for (const QString &name : inputNames) {
		if (std::find(newInputs.begin(), newInputs.end(), name) == newInputs.end()) {
			removed << name;
		}
	}

	inputNames = newInputs;
	outputNames = newOutputs;

	/* Port numbers shift when a device comes or goes, so every port is
	 * opened again by name. */
	OpenPorts();

	if (announce && IsEnabled()) {
		if (!added.isEmpty()) {
			AnnounceNow(QTStr("Accessibility.Midi.Connected").arg(JoinNames(added)));
		}
		if (!removed.isEmpty()) {
			AnnounceNow(QTStr("Accessibility.Midi.Disconnected").arg(JoinNames(removed)));
		}
	}
}

void MidiManager::OpenPorts()
{
	ReleaseHeldHotkeys();
	ClosePorts();
	failedInputs.clear();

	if (!IsEnabled() || exiting) {
		emit PortsChanged();
		return;
	}

	for (size_t i = 0; i < inputNames.size(); i++) {
		const QString &name = inputNames[i];
		if (!portSettings.IsInputEnabled(name)) {
			continue;
		}

		auto port = Midi::InputPort::Open(static_cast<int>(i), name,
						  [this, name](uint32_t message) { Enqueue(name, message); });
		if (port) {
			inputs.push_back(std::move(port));
		} else {
			failedInputs << name;
		}

		QString outputName = OutputFor(name);
		auto index = std::find(outputNames.begin(), outputNames.end(), outputName);
		if (outputName.isEmpty() || outputs.count(outputName) || index == outputNames.end()) {
			continue;
		}
		auto output = Midi::OutputPort::Open(static_cast<int>(index - outputNames.begin()), outputName);
		if (output) {
			outputs[outputName] = std::move(output);
		}
	}

	if (!outputs.empty()) {
		feedbackTimer->start();
	}
	emit PortsChanged();
}

void MidiManager::ClosePorts()
{
	feedbackTimer->stop();
	inputs.clear();
	outputs.clear();
	pressed.clear();
	takeovers.clear();
	lastSent.clear();

	std::lock_guard lock(queueMutex);
	queue.clear();
}

/* Runs on a WinMM thread. */
void MidiManager::Enqueue(const QString &port, uint32_t message)
{
	std::lock_guard lock(queueMutex);
	queue.push_back({port, message});
	if (!processingPosted) {
		processingPosted = true;
		QMetaObject::invokeMethod(this, &MidiManager::ProcessQueue, Qt::QueuedConnection);
	}
}

void MidiManager::ProcessQueue()
{
	std::vector<Incoming> batch;
	{
		std::lock_guard lock(queueMutex);
		batch.swap(queue);
		processingPosted = false;
	}

	/* A fader sends many values in a burst; only the last one is applied,
	 * while an encoder's steps add up. */
	std::map<Midi::Control, double> absolute;
	std::map<Midi::Control, int> relative;
	int generation = mappingGeneration;

	for (const Incoming &incoming : batch) {
		Midi::Message message = Midi::ParseShortMessage(incoming.message);
		if (!message.IsValid()) {
			continue;
		}
		blog(LOG_DEBUG, "[MIDI] %s: %08X", QT_TO_UTF8(incoming.port), incoming.message);

		Midi::Control control = Midi::ControlOf(incoming.port, message);
		lastInput[control].start();

		emit MessageReceived(incoming.port, message);

		if (generation != mappingGeneration) {
			absolute.clear();
			relative.clear();
			generation = mappingGeneration;
		}
		if (captureCount > 0 || !IsEnabled() || exiting) {
			continue;
		}

		int index = Midi::FindControl(mapping, control);
		if (index < 0) {
			continue;
		}
		Midi::Assignment assignment = mapping[static_cast<size_t>(index)];

		if (!assignment.target.IsContinuous()) {
			HandleButton(assignment, Midi::IsPress(message));
		} else if (control.type == Midi::ControlType::Note) {
			continue;
		} else if (control.type == Midi::ControlType::ControlChange &&
			   assignment.options.encoder != Midi::EncoderMode::Absolute) {
			relative[control] += Midi::RelativeSteps(assignment.options.encoder, message.value);
		} else {
			absolute[control] = Midi::Normalized(message);
		}
	}

	for (const auto &[control, value] : absolute) {
		int index = Midi::FindControl(mapping, control);
		if (index >= 0) {
			HandleAbsolute(mapping[static_cast<size_t>(index)], value);
		}
	}
	for (const auto &[control, steps] : relative) {
		int index = Midi::FindControl(mapping, control);
		if (index >= 0 && steps != 0) {
			HandleRelative(mapping[static_cast<size_t>(index)], steps);
		}
	}
}

void MidiManager::HandleButton(const Midi::Assignment &assignment, bool down)
{
	const Midi::Control &control = assignment.control;
	bool &wasDown = pressed[control];
	if (down == wasDown) {
		return;
	}
	wasDown = down;

	if (!down) {
		auto held = heldHotkeys.find(control);
		if (held != heldHotkeys.end()) {
			StreamDeck::Trigger(held->second, false);
			heldHotkeys.erase(held);
		}
		return;
	}

	const Midi::Target &target = assignment.target;
	if (!Midi::IsAvailable(target)) {
		AnnounceUnavailable(target);
		return;
	}

	if (target.kind != Midi::TargetKind::Hotkey) {
		Midi::ToggleTarget(target);
		AnnounceNow(Midi::DescribeValue(target));
		return;
	}

	StreamDeck::ResolvedAction resolved = StreamDeck::Resolve(target.hotkey);
	if (!resolved.IsValid()) {
		AnnounceUnavailable(target);
		return;
	}

	/* A toggle acts on the press. Other hotkeys are held for as long as
	 * the button, for push-to-talk. */
	if (target.hotkey.toggle) {
		StreamDeck::Toggle(resolved);
	} else {
		StreamDeck::Trigger(resolved.id, true);
		heldHotkeys[control] = resolved.id;
	}

	QString feedback = StreamDeck::Feedback(target.hotkey, QString());
	if (!feedback.isEmpty()) {
		AnnounceNow(feedback);
	}
}

void MidiManager::HandleAbsolute(const Midi::Assignment &assignment, double value)
{
	const Midi::Options &options = assignment.options;
	const Midi::Target &target = assignment.target;

	if (options.invert) {
		value = 1.0 - value;
	}
	value = options.low + value * (options.high - options.low);

	if (target.kind == Midi::TargetKind::TBar) {
		MoveTBar(value);
		return;
	}

	double current = Midi::ReadValue(target);
	if (current < 0.0) {
		AnnounceUnavailable(target);
		return;
	}

	if (options.takeover && assignment.control.type != Midi::ControlType::PitchBend) {
		Midi::Takeover &takeover = takeovers[assignment.control];
		if (!takeover.Accept(value, current)) {
			AnnounceTakeover(target, value < current);
			return;
		}
		Midi::ApplyValue(target, value);
		takeover.Applied(Midi::ReadValue(target));
	} else {
		Midi::ApplyValue(target, value);
	}
	ScheduleValueAnnouncement(target);
}

void MidiManager::HandleRelative(const Midi::Assignment &assignment, int steps)
{
	const Midi::Options &options = assignment.options;
	const Midi::Target &target = assignment.target;

	double current = Midi::ReadValue(target);
	if (current < 0.0) {
		AnnounceUnavailable(target);
		return;
	}

	double change = steps * Midi::EncoderStep(target) * (options.invert ? -1.0 : 1.0);
	double low = std::min(options.low, options.high);
	double high = std::max(options.low, options.high);
	double value = std::clamp(current + change, low, high);

	if (target.kind == Midi::TargetKind::TBar) {
		MoveTBar(value);
		return;
	}

	Midi::ApplyValue(target, value);
	ScheduleValueAnnouncement(target);
}

/* Moving the T-bar with a fader has no release either, so the ends of its
 * travel finish or cancel the transition, as in the Controls panel. */
void MidiManager::MoveTBar(double value)
{
	OBSBasic *main = OBSBasic::Get();
	if (!main || !obs_frontend_preview_program_mode_active()) {
		Midi::Target target;
		target.kind = Midi::TargetKind::TBar;
		AnnounceUnavailable(target);
		return;
	}

	int maximum = OBSAccessibleBridge::TBarMaximum();
	double position = tBarReversed ? 1.0 - value : value;

	if (position >= tBarEnd) {
		OBSAccessibleBridge::SetTBarPosition(main, maximum);
		OBSAccessibleBridge::ReleaseTBar(main);
		tBarReversed = !tBarReversed;
		tBarMoving = false;
		return;
	}

	if (position <= tBarStart) {
		if (tBarMoving) {
			OBSAccessibleBridge::SetTBarPosition(main, 0);
			OBSAccessibleBridge::ReleaseTBar(main);
			tBarMoving = false;
		}
		return;
	}

	OBSAccessibleBridge::SetTBarPosition(main, static_cast<int>(std::lround(position * maximum)));
	tBarMoving = true;
}

void MidiManager::ReleaseHeldHotkeys()
{
	for (const auto &[control, id] : heldHotkeys) {
		StreamDeck::Trigger(id, false);
	}
	heldHotkeys.clear();
}

void MidiManager::AnnounceNow(const QString &text)
{
	Announcer *announcer = Announcer::Get();
	if (announcer && GetAnnounceValues() && !text.isEmpty()) {
		announcer->AnnounceAtOnce(text);
	}
}

void MidiManager::ScheduleValueAnnouncement(const Midi::Target &target)
{
	pendingAnnouncement = target;
	announceTimer->start();
}

void MidiManager::AnnounceTakeover(const Midi::Target &target, bool up)
{
	if (lastTakeoverAnnouncement.isValid() &&
	    lastTakeoverAnnouncement.elapsed() < takeoverAnnouncementMilliseconds) {
		return;
	}
	lastTakeoverAnnouncement.start();

	const char *lookupKey = up ? "Accessibility.Midi.Takeover.Up" : "Accessibility.Midi.Takeover.Down";
	AnnounceNow(QTStr(lookupKey).arg(Midi::DescribeValue(target)));
}

void MidiManager::AnnounceUnavailable(const Midi::Target &target)
{
	if (lastUnavailableAnnouncement.isValid() &&
	    lastUnavailableAnnouncement.elapsed() < takeoverAnnouncementMilliseconds) {
		return;
	}
	lastUnavailableAnnouncement.start();
	AnnounceNow(QTStr("Accessibility.Midi.Unavailable").arg(Midi::DescribeTarget(target)));
}

void MidiManager::SendFeedback()
{
	if (exiting || !IsEnabled()) {
		return;
	}

	for (const Midi::Assignment &assignment : mapping) {
		const Midi::Control &control = assignment.control;
		const Midi::Options &options = assignment.options;
		if (!options.feedback || assignment.target.kind == Midi::TargetKind::TBar) {
			continue;
		}

		auto output = outputs.find(OutputFor(control.port));
		if (output == outputs.end()) {
			continue;
		}

		auto input = lastInput.find(control);
		if (input != lastInput.end() && input->second.elapsed() < echoMilliseconds) {
			continue;
		}

		double state = Midi::ReadValue(assignment.target);
		if (state < 0.0) {
			continue;
		}

		int value = 0;
		if (assignment.target.IsContinuous()) {
			double range = options.high - options.low;
			double position = std::abs(range) > 1e-9 ? (state - options.low) / range : 0.0;
			position = std::clamp(position, 0.0, 1.0);
			if (options.invert) {
				position = 1.0 - position;
			}
			value = Midi::ValueFromNormalized(control.type, position);
		} else {
			value = state > 0.5 ? options.onValue : options.offValue;
		}

		auto sent = lastSent.find(control);
		if (sent != lastSent.end() && sent->second == value) {
			continue;
		}
		if (output->second->Send(Midi::PackShortMessage(Midi::MessageFor(control, value)))) {
			lastSent[control] = value;
		}
	}
}

/* Turns off the lights of every key, so that none stays lit for something
 * nothing controls any more. */
void MidiManager::ClearFeedback()
{
	for (const Midi::Assignment &assignment : mapping) {
		if (!assignment.options.feedback || assignment.target.IsContinuous()) {
			continue;
		}
		auto output = outputs.find(OutputFor(assignment.control.port));
		if (output != outputs.end()) {
			output->second->Send(Midi::PackShortMessage(
				Midi::MessageFor(assignment.control, assignment.options.offValue)));
		}
	}
	lastSent.clear();
}

void MidiManager::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto manager = static_cast<MidiManager *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		/* Sends every state again. */
		manager->lastSent.clear();
		manager->takeovers.clear();
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		manager->tBarReversed = false;
		manager->tBarMoving = false;
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		manager->ReleaseHeldHotkeys();
		manager->ClearFeedback();
		manager->exiting = true;
		manager->scanTimer->stop();
		manager->feedbackTimer->stop();
		manager->announceTimer->stop();
		manager->ClosePorts();
		break;
	default:
		break;
	}
}

} // namespace OBS::Accessibility
