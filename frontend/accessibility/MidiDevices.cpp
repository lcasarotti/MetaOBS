#include "MidiDevices.hpp"

#include <util/base.h>

#include <map>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmsystem.h>

namespace OBS::Accessibility::Midi {

namespace {
std::vector<QString> Disambiguate(const std::vector<QString> &names)
{
	std::vector<QString> result;
	std::map<QString, int> seen;
	for (const QString &name : names) {
		int count = ++seen[name];
		result.push_back(count == 1 ? name : QStringLiteral("%1 %2").arg(name).arg(count));
	}
	return result;
}
} // namespace

std::vector<QString> InputPortNames()
{
	std::vector<QString> names;
	UINT count = midiInGetNumDevs();
	for (UINT i = 0; i < count; i++) {
		MIDIINCAPSW caps = {};
		if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			names.push_back(QString::fromWCharArray(caps.szPname));
		} else {
			names.push_back(QStringLiteral("MIDI %1").arg(i + 1));
		}
	}
	return Disambiguate(names);
}

std::vector<QString> OutputPortNames()
{
	std::vector<QString> names;
	UINT count = midiOutGetNumDevs();
	for (UINT i = 0; i < count; i++) {
		MIDIOUTCAPSW caps = {};
		if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			names.push_back(QString::fromWCharArray(caps.szPname));
		} else {
			names.push_back(QStringLiteral("MIDI %1").arg(i + 1));
		}
	}
	return Disambiguate(names);
}

InputPort::InputPort(const QString &name_, Callback callback_) : name(name_), callback(std::move(callback_)) {}

std::unique_ptr<InputPort> InputPort::Open(int index, const QString &name, Callback callback)
{
	std::unique_ptr<InputPort> port(new InputPort(name, std::move(callback)));

	HMIDIIN handle = nullptr;
	MMRESULT result = midiInOpen(&handle, static_cast<UINT>(index), reinterpret_cast<DWORD_PTR>(&OnMessage),
				     reinterpret_cast<DWORD_PTR>(port.get()), CALLBACK_FUNCTION);
	if (result != MMSYSERR_NOERROR) {
		blog(LOG_WARNING, "[MIDI] Could not open input '%s' (error %u)", name.toUtf8().constData(),
		     static_cast<unsigned>(result));
		return nullptr;
	}

	port->handle = handle;
	midiInStart(handle);
	blog(LOG_INFO, "[MIDI] Opened input '%s'", name.toUtf8().constData());
	return port;
}

InputPort::~InputPort()
{
	if (!handle) {
		return;
	}

	auto midiIn = static_cast<HMIDIIN>(handle);
	midiInStop(midiIn);
	midiInReset(midiIn);
	midiInClose(midiIn);
	blog(LOG_INFO, "[MIDI] Closed input '%s'", name.toUtf8().constData());
}

void __stdcall InputPort::OnMessage(void *, unsigned int message, uintptr_t instance, uintptr_t param1, uintptr_t)
{
	if (message != MIM_DATA) {
		return;
	}

	auto port = reinterpret_cast<InputPort *>(instance);
	if (port && port->callback) {
		port->callback(static_cast<uint32_t>(param1));
	}
}

std::unique_ptr<OutputPort> OutputPort::Open(int index, const QString &name)
{
	std::unique_ptr<OutputPort> port(new OutputPort(name));

	HMIDIOUT handle = nullptr;
	MMRESULT result = midiOutOpen(&handle, static_cast<UINT>(index), 0, 0, CALLBACK_NULL);
	if (result != MMSYSERR_NOERROR) {
		blog(LOG_WARNING, "[MIDI] Could not open output '%s' (error %u)", name.toUtf8().constData(),
		     static_cast<unsigned>(result));
		return nullptr;
	}

	port->handle = handle;
	blog(LOG_INFO, "[MIDI] Opened output '%s'", name.toUtf8().constData());
	return port;
}

OutputPort::~OutputPort()
{
	if (handle) {
		midiOutClose(static_cast<HMIDIOUT>(handle));
	}
}

bool OutputPort::Send(uint32_t message)
{
	return handle && midiOutShortMsg(static_cast<HMIDIOUT>(handle), message) == MMSYSERR_NOERROR;
}

} // namespace OBS::Accessibility::Midi
