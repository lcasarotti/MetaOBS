#pragma once

#include "StreamDeckProtocol.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace OBS::Accessibility::StreamDeck {

struct DeviceInfo {
	std::wstring path;
	const Model *model;
};

/* The connected Stream Decks of a known model. */
std::vector<DeviceInfo> EnumerateDevices();

/* An open Stream Deck (Windows HID). The device is opened shared, so other
 * programs, the Elgato app among them, still receive its key reports. Read can
 * block on one thread while Cancel, SetFeature and Write are called from
 * another. */
class HidDevice {
public:
	static std::unique_ptr<HidDevice> Open(const DeviceInfo &info);
	~HidDevice();

	HidDevice(const HidDevice &) = delete;
	HidDevice &operator=(const HidDevice &) = delete;

	const Model &GetModel() const { return *model; }

	/* Waits for the next input report. Returns false once the device has
	 * been unplugged or Cancel has been called. */
	bool Read(std::vector<uint8_t> &report);
	void Cancel();

	bool SetFeature(const std::vector<uint8_t> &report);
	bool Write(const std::vector<uint8_t> &report);

private:
	struct Handles;

	HidDevice(const Model *model, std::unique_ptr<Handles> handles);

	const Model *model;
	std::unique_ptr<Handles> handles;
};

/* Whether the Elgato Stream Deck app is running. It would run its own actions
 * for the keys too. */
bool IsElgatoAppRunning();

} // namespace OBS::Accessibility::StreamDeck
