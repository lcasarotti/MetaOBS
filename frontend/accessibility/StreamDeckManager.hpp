#pragma once

#include "StreamDeckHid.hpp"

#include <QObject>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class QTimer;

namespace OBS::Accessibility {

/* Reads an Elgato Stream Deck directly, without the Elgato app, so that its
 * keys can run OBS actions set up in an accessible dialog. It looks for a
 * device every few seconds until one is plugged in, reads its keys on a
 * thread of its own, and reports them on the UI thread. Windows only. */
class StreamDeckManager : public QObject {
	Q_OBJECT

public:
	explicit StreamDeckManager(QObject *parent);
	~StreamDeckManager() override;

	static StreamDeckManager *Get();

	/* The connected device's model, or nullptr. */
	const StreamDeck::Model *ConnectedModel() const;

signals:
	void KeyChanged(int key, bool pressed);
	void ConnectionChanged();

private:
	void Scan(bool announce);
	void StartReading(std::unique_ptr<StreamDeck::HidDevice> newDevice);
	void StopReading();
	void ReadLoop();
	void HandleKeyStates(const std::vector<bool> &states);
	void HandleDeviceLost();

	QTimer *scanTimer = nullptr;
	std::unique_ptr<StreamDeck::HidDevice> device;
	std::thread reader;
	std::atomic<bool> stopping = false;
	std::vector<bool> keyStates;
};

} // namespace OBS::Accessibility
