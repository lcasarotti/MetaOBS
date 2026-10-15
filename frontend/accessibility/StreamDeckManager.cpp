#include "StreamDeckManager.hpp"
#include "Announcer.hpp"
#include "StreamDeckActions.hpp"

#include <OBSApp.hpp>

#include <qt-wrappers.hpp>
#include <util/config-file.h>

#include <QTimer>

#include <string>

#include "moc_StreamDeckManager.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int scanIntervalMilliseconds = 2000;
/* The first reports of each connection are logged in full, to check the
 * report layout on devices that have not been tried yet. */
constexpr int rawReportsToLog = 12;
/* Keys are set up for this model until a Stream Deck is connected. */
constexpr uint16_t defaultProductId = 0x0090;

constexpr const char *section = "StreamDeck";
constexpr const char *enabledKey = "Enabled";
constexpr const char *announceActionsKey = "AnnounceActions";
constexpr const char *keysKey = "Keys";
constexpr const char *productIdKey = "ProductId";

StreamDeckManager *instance = nullptr;

config_t *GetConfig()
{
	return App()->GetUserConfig();
}

std::string ToHex(const std::vector<uint8_t> &bytes)
{
	static const char digits[] = "0123456789ABCDEF";

	std::string text;
	text.reserve(bytes.size() * 3);
	for (uint8_t byte : bytes) {
		if (!text.empty()) {
			text += ' ';
		}
		text += digits[byte >> 4];
		text += digits[byte & 0x0F];
	}
	return text;
}

void Announce(const QString &text)
{
	if (Announcer *announcer = Announcer::Get()) {
		announcer->Announce(text);
	}
}
} // namespace

StreamDeckManager::StreamDeckManager(QObject *parent) : QObject(parent)
{
	instance = this;

	config_t *config = GetConfig();
	config_set_default_bool(config, section, enabledKey, true);
	config_set_default_bool(config, section, announceActionsKey, true);
	config_set_default_int(config, section, productIdKey, defaultProductId);

	const char *json = config_get_string(config, section, keysKey);
	mapping = StreamDeck::MappingFromJson(QT_UTF8(json ? json : ""));

	scanTimer = new QTimer(this);
	scanTimer->setInterval(scanIntervalMilliseconds);
	connect(scanTimer, &QTimer::timeout, this, [this]() { Scan(true); });

	/* A device that is already plugged in when OBS starts is not
	 * announced. */
	QTimer::singleShot(0, this, [this]() {
		Scan(false);
		if (!device) {
			scanTimer->start();
		}
	});
}

StreamDeckManager::~StreamDeckManager()
{
	StopReading();
	if (instance == this) {
		instance = nullptr;
	}
}

StreamDeckManager *StreamDeckManager::Get()
{
	return instance;
}

const StreamDeck::Model *StreamDeckManager::ConnectedModel() const
{
	return device ? &device->GetModel() : nullptr;
}

const StreamDeck::Model &StreamDeckManager::ConfiguredModel() const
{
	if (device) {
		return device->GetModel();
	}

	auto productId = static_cast<uint16_t>(config_get_int(GetConfig(), section, productIdKey));
	const StreamDeck::Model *model = StreamDeck::FindModel(StreamDeck::elgatoVendorId, productId);
	if (!model) {
		model = StreamDeck::FindModel(StreamDeck::elgatoVendorId, defaultProductId);
	}
	return *model;
}

bool StreamDeckManager::IsEnabled() const
{
	return config_get_bool(GetConfig(), section, enabledKey);
}

void StreamDeckManager::SetEnabled(bool enabled)
{
	config_set_bool(GetConfig(), section, enabledKey, enabled);
	SaveConfig();
}

bool StreamDeckManager::GetAnnounceActions() const
{
	return config_get_bool(GetConfig(), section, announceActionsKey);
}

void StreamDeckManager::SetAnnounceActions(bool announce)
{
	config_set_bool(GetConfig(), section, announceActionsKey, announce);
	SaveConfig();
}

void StreamDeckManager::SetMapping(const StreamDeck::Mapping &newMapping)
{
	mapping = newMapping;
	config_set_string(GetConfig(), section, keysKey, QT_TO_UTF8(StreamDeck::MappingToJson(mapping)));
	SaveConfig();
}

void StreamDeckManager::SaveConfig()
{
	config_save_safe(GetConfig(), "tmp", nullptr);
}

void StreamDeckManager::SetCaptured(bool captured_)
{
	captured = captured_;
}

void StreamDeckManager::Scan(bool announce)
{
	if (device) {
		return;
	}

	for (const StreamDeck::DeviceInfo &info : StreamDeck::EnumerateDevices()) {
		std::unique_ptr<StreamDeck::HidDevice> opened = StreamDeck::HidDevice::Open(info);
		if (!opened) {
			continue;
		}

		if (StreamDeck::IsElgatoAppRunning()) {
			blog(LOG_WARNING, "[StreamDeck] The Elgato Stream Deck app is running and acts on keys too");
		}

		const char *name = opened->GetModel().name;
		config_set_int(GetConfig(), section, productIdKey, opened->GetModel().productId);
		StartReading(std::move(opened));
		scanTimer->stop();

		if (announce) {
			Announce(QTStr("Accessibility.StreamDeck.Connected").arg(QT_UTF8(name)));
		}
		emit ConnectionChanged();
		return;
	}
}

void StreamDeckManager::StartReading(std::unique_ptr<StreamDeck::HidDevice> newDevice)
{
	device = std::move(newDevice);
	keyStates.assign(static_cast<size_t>(device->GetModel().keyCount), false);
	stopping = false;
	reader = std::thread([this]() { ReadLoop(); });
}

void StreamDeckManager::StopReading()
{
	if (!device) {
		return;
	}

	stopping = true;
	device->Cancel();
	if (reader.joinable()) {
		reader.join();
	}
	device.reset();
	keyStates.clear();
}

/* Runs on the reader thread. */
void StreamDeckManager::ReadLoop()
{
	const StreamDeck::Model &model = device->GetModel();
	std::vector<uint8_t> report;
	int rawReportsLogged = 0;

	while (device->Read(report)) {
		if (rawReportsLogged < rawReportsToLog) {
			blog(LOG_DEBUG, "[StreamDeck] Report: %s", ToHex(report).c_str());
			rawReportsLogged++;
		}

		std::vector<bool> states = StreamDeck::ParseKeyStates(model, report);
		if (states.empty()) {
			continue;
		}

		QMetaObject::invokeMethod(
			this, [this, states]() { HandleKeyStates(states); }, Qt::QueuedConnection);
	}

	if (!stopping) {
		QMetaObject::invokeMethod(this, [this]() { HandleDeviceLost(); }, Qt::QueuedConnection);
	}
}

void StreamDeckManager::HandleKeyStates(const std::vector<bool> &states)
{
	if (!device || states.size() != keyStates.size()) {
		return;
	}

	for (size_t i = 0; i < states.size(); i++) {
		if (states[i] != keyStates[i]) {
			keyStates[i] = states[i];
			HandleKey(static_cast<int>(i), states[i]);
		}
	}
}

void StreamDeckManager::HandleKey(int key, bool pressed)
{
	emit KeyChanged(key, pressed);

	if (!pressed) {
		auto held = heldHotkeys.find(key);
		if (held != heldHotkeys.end()) {
			StreamDeck::Trigger(held->second, false);
			heldHotkeys.erase(held);
		}
		return;
	}

	if (captured || !IsEnabled()) {
		return;
	}

	auto assignment = mapping.find(key);
	if (assignment == mapping.end()) {
		return;
	}

	/* A new key stops what the last one is still saying. */
	Announcer *announcer = Announcer::Get();
	if (announcer && GetAnnounceActions()) {
		announcer->Interrupt();
	}

	const StreamDeck::KeyAssignment &keyAssignment = assignment->second;
	StreamDeck::ResolvedAction resolved = StreamDeck::Resolve(keyAssignment.action);
	if (!resolved.IsValid()) {
		if (announcer) {
			announcer->AnnounceAtOnce(StreamDeck::Describe(keyAssignment.action));
		}
		return;
	}

	/* A toggle acts on the press. Other hotkeys are held for as long as
	 * the key, for push-to-talk. */
	if (keyAssignment.action.toggle) {
		StreamDeck::Toggle(resolved);
	} else {
		StreamDeck::Trigger(resolved.id, true);
		heldHotkeys[key] = resolved.id;
	}

	if (announcer && GetAnnounceActions()) {
		announcer->AnnounceAtOnce(StreamDeck::Feedback(keyAssignment.action, keyAssignment.label));
	}
}

void StreamDeckManager::HandleDeviceLost()
{
	if (!device) {
		return;
	}

	const char *name = device->GetModel().name;
	blog(LOG_INFO, "[StreamDeck] %s disconnected", name);

	/* Releases the keys that were held, so that push-to-talk does not
	 * stay on. */
	for (size_t i = 0; i < keyStates.size(); i++) {
		if (keyStates[i]) {
			HandleKey(static_cast<int>(i), false);
		}
	}

	StopReading();
	Announce(QTStr("Accessibility.StreamDeck.Disconnected").arg(QT_UTF8(name)));
	emit ConnectionChanged();
	scanTimer->start();
}

} // namespace OBS::Accessibility
