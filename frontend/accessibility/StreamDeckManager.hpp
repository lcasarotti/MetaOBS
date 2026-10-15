#pragma once

#include "StreamDeckHid.hpp"
#include "StreamDeckMapping.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QObject>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
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

	/* Brightness of the key displays, in percent. Set save to false to
	 * try a value without keeping it. */
	int GetBrightness() const;
	void SetBrightness(int percent, bool save);

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
	void StopReading(bool clearDisplays = false);
	void ReadLoop();
	void HandleKeyStates(const std::vector<bool> &states);
	void HandleKey(int key, bool pressed);
	void HandleDeviceLost();
	void SaveConfig();

	/* Shows each key's label on its display. Only keys whose label or
	 * availability changed are sent again, unless force is set. */
	void RefreshKeyImages(bool force = false);
	void ScheduleKeyImageRefresh();
	void WriteLoop();
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	static void OnSourceSignal(void *data, calldata_t *);

	QTimer *scanTimer = nullptr;
	std::unique_ptr<StreamDeck::HidDevice> device;
	std::thread reader;
	std::atomic<bool> stopping = false;
	std::vector<bool> keyStates;

	/* Images are sent on a thread of their own, since a key takes about
	 * twenty reports. Newer images replace pending ones for the same
	 * key. */
	std::thread writer;
	std::mutex writerMutex;
	std::condition_variable writerWake;
	bool writerStopping = false;
	std::map<int, std::vector<std::vector<uint8_t>>> pendingImages;
	/* What each key shows, to send only what changed. */
	std::map<int, QString> shownImages;
	QTimer *imageRefreshTimer = nullptr;
	/* Set once OBS starts shutting down and hotkeys go away. */
	bool exiting = false;
	std::vector<OBSSignal> sourceSignals;

	StreamDeck::Mapping mapping;
	bool captured = false;
	/* The hotkeys pressed by keys that are still held, to release the same
	 * ones even if the assignment changes meanwhile. */
	std::map<int, obs_hotkey_id> heldHotkeys;
};

} // namespace OBS::Accessibility
