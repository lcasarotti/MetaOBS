#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

/* MIDI ports through WinMM. Windows only. */
namespace OBS::Accessibility::Midi {

/* The ports' names, in WinMM's order, so that a name's index opens it. Two
 * ports with the same name get " 2", " 3"... after the first. */
std::vector<QString> InputPortNames();
std::vector<QString> OutputPortNames();

class InputPort {
public:
	/* Called on a WinMM thread with each short message. */
	using Callback = std::function<void(uint32_t message)>;

	/* Nullptr if the port cannot be opened, for instance because another
	 * program uses it. */
	static std::unique_ptr<InputPort> Open(int index, const QString &name, Callback callback);
	~InputPort();

	const QString &Name() const { return name; }

private:
	InputPort(const QString &name, Callback callback);
	static void __stdcall OnMessage(void *handle, unsigned int message, uintptr_t instance, uintptr_t param1,
					uintptr_t param2);

	QString name;
	Callback callback;
	void *handle = nullptr;
};

class OutputPort {
public:
	static std::unique_ptr<OutputPort> Open(int index, const QString &name);
	~OutputPort();

	const QString &Name() const { return name; }
	bool Send(uint32_t message);

private:
	explicit OutputPort(const QString &name) : name(name) {}

	QString name;
	void *handle = nullptr;
};

} // namespace OBS::Accessibility::Midi
