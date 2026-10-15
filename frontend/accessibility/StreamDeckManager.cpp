#include "StreamDeckManager.hpp"
#include "Announcer.hpp"
#include "StreamDeckActions.hpp"
#include "StreamDeckImages.hpp"

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
constexpr int defaultBrightness = 60;
/* Sources come and go in bursts, when a scene collection loads, for
 * instance. */
constexpr int imageRefreshDelayMilliseconds = 300;

constexpr const char *section = "StreamDeck";
constexpr const char *enabledKey = "Enabled";
constexpr const char *announceActionsKey = "AnnounceActions";
constexpr const char *keysKey = "Keys";
constexpr const char *productIdKey = "ProductId";
constexpr const char *brightnessKey = "Brightness";

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
	config_set_default_int(config, section, brightnessKey, defaultBrightness);

	const char *json = config_get_string(config, section, keysKey);
	mapping = StreamDeck::MappingFromJson(QT_UTF8(json ? json : ""));

	imageRefreshTimer = new QTimer(this);
	imageRefreshTimer->setSingleShot(true);
	imageRefreshTimer->setInterval(imageRefreshDelayMilliseconds);
	connect(imageRefreshTimer, &QTimer::timeout, this, [this]() { RefreshKeyImages(); });

	/* A key's action becomes unavailable, or available again, when its
	 * source is removed, added or renamed. */
	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : {"source_create", "source_remove", "source_rename"}) {
		sourceSignals.emplace_back(handler, signal, OnSourceSignal, this);
	}
	obs_frontend_add_event_callback(OnFrontendEvent, this);

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
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	sourceSignals.clear();

	/* Clears the displays, which would otherwise show actions nothing
	 * runs any more. */
	StopReading(true);
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
	RefreshKeyImages();
}

int StreamDeckManager::GetBrightness() const
{
	return static_cast<int>(config_get_int(GetConfig(), section, brightnessKey));
}

void StreamDeckManager::SetBrightness(int percent, bool save)
{
	if (device) {
		device->SetFeature(StreamDeck::BrightnessReport(device->GetModel(), percent));
	}
	if (save) {
		config_set_int(GetConfig(), section, brightnessKey, percent);
		SaveConfig();
	}
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
	RefreshKeyImages();
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

	writerStopping = false;
	writer = std::thread([this]() { WriteLoop(); });

	SetBrightness(GetBrightness(), false);
	RefreshKeyImages(true);
}

void StreamDeckManager::StopReading(bool clearDisplays)
{
	if (!device) {
		return;
	}

	{
		std::lock_guard lock(writerMutex);
		writerStopping = true;
		pendingImages.clear();
	}
	writerWake.notify_all();
	if (writer.joinable()) {
		writer.join();
	}

	if (clearDisplays) {
		device->SetFeature(StreamDeck::ResetReport(device->GetModel()));
	}

	stopping = true;
	device->Cancel();
	if (reader.joinable()) {
		reader.join();
	}
	device.reset();
	keyStates.clear();
	shownImages.clear();
}

void StreamDeckManager::RefreshKeyImages(bool force)
{
	if (!device || exiting) {
		return;
	}

	const StreamDeck::Model &model = device->GetModel();
	bool enabled = IsEnabled();

	for (int key = 0; key < model.keyCount; key++) {
		QString label;
		bool available = false;

		auto assignment = mapping.find(key);
		if (enabled && assignment != mapping.end()) {
			const StreamDeck::KeyAssignment &keyAssignment = assignment->second;
			available = StreamDeck::Resolve(keyAssignment.action).IsValid();
			label = keyAssignment.label.isEmpty() ? StreamDeck::Describe(keyAssignment.action)
							      : keyAssignment.label;
		}

		/* Tells apart the same label with and without its action. */
		QString shown = (available ? "1" : "0") + label;
		auto previous = shownImages.find(key);
		if (!force && previous != shownImages.end() && previous->second == shown) {
			continue;
		}
		shownImages[key] = shown;

		QImage image = StreamDeck::RenderLabel(model, label, available);
		std::vector<uint8_t> encoded = StreamDeck::EncodeKeyImage(model, image);
		{
			std::lock_guard lock(writerMutex);
			pendingImages[key] = StreamDeck::ImageReports(model, key, encoded);
		}
		writerWake.notify_all();
	}
}

void StreamDeckManager::ScheduleKeyImageRefresh()
{
	if (!exiting) {
		imageRefreshTimer->start();
	}
}

/* Runs on the writer thread. */
void StreamDeckManager::WriteLoop()
{
	for (;;) {
		std::vector<std::vector<uint8_t>> reports;
		{
			std::unique_lock lock(writerMutex);
			writerWake.wait(lock, [this]() { return writerStopping || !pendingImages.empty(); });
			if (writerStopping) {
				return;
			}

			auto next = pendingImages.begin();
			reports = std::move(next->second);
			pendingImages.erase(next);
		}

		for (const std::vector<uint8_t> &report : reports) {
			if (!device->Write(report)) {
				blog(LOG_WARNING, "[StreamDeck] Could not send a key image");
				break;
			}
		}
	}
}

void StreamDeckManager::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto manager = static_cast<StreamDeckManager *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		manager->ScheduleKeyImageRefresh();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		manager->exiting = true;
		manager->sourceSignals.clear();
		manager->imageRefreshTimer->stop();
		break;
	default:
		break;
	}
}

/* Source signals come from any thread. */
void StreamDeckManager::OnSourceSignal(void *data, calldata_t *)
{
	auto manager = static_cast<StreamDeckManager *>(data);
	QMetaObject::invokeMethod(manager, &StreamDeckManager::ScheduleKeyImageRefresh, Qt::QueuedConnection);
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
