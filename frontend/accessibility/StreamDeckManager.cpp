#include "StreamDeckManager.hpp"
#include "Announcer.hpp"

#include <OBSApp.hpp>

#include <qt-wrappers.hpp>

#include <QTimer>

#include <string>

#include "moc_StreamDeckManager.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int scanIntervalMilliseconds = 2000;
/* The first reports of each connection are logged in full, to check the
 * report layout on devices that have not been tried yet. */
constexpr int rawReportsToLog = 12;

StreamDeckManager *instance = nullptr;

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
			blog(LOG_WARNING, "[StreamDeck] The Elgato Stream Deck app is running and will also act on keys");
		}

		const char *name = opened->GetModel().name;
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
			blog(LOG_INFO, "[StreamDeck] Report: %s", ToHex(report).c_str());
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

	const StreamDeck::Model &model = device->GetModel();
	for (size_t i = 0; i < states.size(); i++) {
		if (states[i] == keyStates[i]) {
			continue;
		}
		keyStates[i] = states[i];

		int key = static_cast<int>(i);
		StreamDeck::KeyPosition position = StreamDeck::PositionOf(model, key);
		blog(LOG_INFO, "[StreamDeck] Key %d (row %d, column %d) %s", key + 1, position.row, position.column,
		     states[i] ? "pressed" : "released");

		emit KeyChanged(key, states[i]);
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
			emit KeyChanged(static_cast<int>(i), false);
		}
	}

	StopReading();
	Announce(QTStr("Accessibility.StreamDeck.Disconnected").arg(QT_UTF8(name)));
	emit ConnectionChanged();
	scanTimer->start();
}

} // namespace OBS::Accessibility
