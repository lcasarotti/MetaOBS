#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

/* MIDI messages, the controls that send them, and the ways their values are
 * read. Kept free of libobs and Windows so it can be tested. */
namespace OBS::Accessibility::Midi {

enum class MessageType {
	Invalid = 0,
	NoteOff = 1,
	NoteOn = 2,
	ControlChange = 3,
	PitchBend = 4,
};

struct Message {
	MessageType type = MessageType::Invalid;
	/* From 0 to 15; people count channels from 1. */
	int channel = 0;
	/* The note or controller number; 0 for pitch bend. */
	int number = 0;
	/* From 0 to 127, or to 16383 for pitch bend. */
	int value = 0;

	bool IsValid() const { return type != MessageType::Invalid; }
};

/* A short message packed as WinMM delivers it, status byte first. Messages
 * MetaOBS does not use come back invalid. */
Message ParseShortMessage(uint32_t packed);
uint32_t PackShortMessage(const Message &message);

enum class ControlType {
	Invalid = 0,
	Note = 1,
	ControlChange = 2,
	PitchBend = 3,
};

/* A key, button, knob or fader of a controller: what it sends, and on which
 * port, since two controllers may send the same messages. */
struct Control {
	QString port;
	ControlType type = ControlType::Invalid;
	int channel = 0;
	int number = 0;

	bool IsValid() const { return type != ControlType::Invalid && !port.isEmpty(); }
	bool operator==(const Control &other) const;
	bool operator!=(const Control &other) const { return !(*this == other); }
	bool operator<(const Control &other) const;
};

Control ControlOf(const QString &port, const Message &message);

int MaximumValue(ControlType type);

/* Whether a message presses a key or button rather than releasing it. A note
 * on with velocity 0 releases, and so does a controller below 64. */
bool IsPress(const Message &message);

/* The message's value from 0 to 1. */
double Normalized(const Message &message);

/* The message that shows a value from 0 to 1 on a control: lights a key or
 * moves a motorized fader. Notes are sent as note on, even when off. */
Message MessageFor(const Control &control, int value);
int ValueFromNormalized(ControlType type, double value);

/* "C3" for note 60, as most music programs name it. The names are given in
 * order from C. */
QString NoteName(int note, const QStringList &names);

/* How a knob reports its turns. Absolute knobs send their position; endless
 * encoders send how far they turned, in one of several codes. */
enum class EncoderMode {
	Invalid = 0,
	Absolute = 1,
	/* 1 to 63 clockwise, 127 down to 65 counterclockwise. */
	TwosComplement = 2,
	/* 65 and above clockwise, 63 and below counterclockwise. */
	BinaryOffset = 3,
	/* 1 to 63 clockwise, 65 to 127 counterclockwise. */
	SignMagnitude = 4,
};

/* The steps a relative encoder turned, positive clockwise. */
int RelativeSteps(EncoderMode mode, int value);

enum class Gesture {
	Invalid = 0,
	Button = 1,
	Continuous = 2,
};

/* Tells from the first messages a control sends whether it is a key or button
 * or a knob or fader, and whether a knob is an endless encoder. Only the
 * messages of one control should be added. */
class GestureClassifier {
public:
	void Add(const Message &message);
	void Clear() { messages.clear(); }
	bool IsEmpty() const { return messages.empty(); }

	Gesture Result() const;
	EncoderMode Encoder() const;

private:
	/* How many times in a row the same value came, at most. */
	int LongestRepeat() const;

	std::vector<Message> messages;
};

/* Keeps a fader that does not match the value it controls from making the
 * value jump: the fader takes over once it reaches the value or crosses it.
 * Values are from 0 to 1. */
class Takeover {
public:
	static constexpr double tolerance = 2.0 / 127.0;

	/* Whether the fader drives the value now. A value changed by other means
	 * since the last Applied() makes the fader catch up with it again. */
	bool Accept(double incoming, double current);
	/* The value after the fader set it. */
	void Applied(double value) { lastApplied = value; }
	void Reset();

private:
	bool engaged = false;
	double lastIncoming = -1.0;
	double lastApplied = -1.0;
};

/* The position of the mixer's volume faders, from 0 to 1, for a volume, and
 * back. The curve matches OBS's own faders: 0 dB at the top. */
double VolumeDbToPosition(double db);
double PositionToVolumeDb(double position);

} // namespace OBS::Accessibility::Midi
