#pragma once

#include "StreamDeckHid.hpp"
#include "StreamDeckMapping.hpp"

#include <obs.h>

#include <QObject>

#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

class QTimer;

namespace OBS::Accessibility {

/* Reads an Elgato Stream Deck directly, without the Elgato app, and runs the
 * OBS action assigned to each key. It looks for a device every few seconds
 * until one is plugged in, reads its keys on a thread of its own, and handles
 * them on the UI thread. Windows only. */
class StreamDeckManager : public QObject {
	Q_OBJECT

public:
	explicit StreamDeckManager(QObject *parent);
	~StreamDeckManager() override;

	static StreamDeckManager *Get();

	/* The connected device's model, or nullptr. */
	const StreamDeck::Model *ConnectedModel() const;
	/* The model whose keys are set up: the connected one, otherwise the
	 * last one connected. */
	const StreamDeck::Model &ConfiguredModel() const;

	bool IsEnabled() const;
	void SetEnabled(bool enabled);

	/* Whether pressing a key speaks what its action did. */
	bool GetAnnounceActions() const;
	void SetAnnounceActions(bool announce);

	const StreamDeck::Mapping &GetMapping() const { return mapping; }
	void SetMapping(const StreamDeck::Mapping &newMapping);

	/* While captured, keys are only reported through KeyChanged and run
	 * nothing: the setup dialog uses them to tell which key is which. */
	void SetCaptured(bool captured);

signals:
	void KeyChanged(int key, bool pressed);
	void ConnectionChanged();

private:
	void Scan(bool announce);
	void StartReading(std::unique_ptr<StreamDeck::HidDevice> newDevice);
	void StopReading();
	void ReadLoop();
	void HandleKeyStates(const std::vector<bool> &states);
	void HandleKey(int key, bool pressed);
	void HandleDeviceLost();
	void SaveConfig();

	QTimer *scanTimer = nullptr;
	std::unique_ptr<StreamDeck::HidDevice> device;
	std::thread reader;
	std::atomic<bool> stopping = false;
	std::vector<bool> keyStates;

	StreamDeck::Mapping mapping;
	bool captured = false;
	/* The hotkeys pressed by keys that are still held, to release the same
	 * ones even if the assignment changes meanwhile. */
	std::map<int, obs_hotkey_id> heldHotkeys;
};

} // namespace OBS::Accessibility
