#include "MidiProtocol.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace OBS::Accessibility::Midi {

namespace {
constexpr int noteNamesPerOctave = 12;
/* Note 60, middle C, is C3. */
constexpr int lowestOctave = -2;

/* The curve of OBS's logarithmic faders (obs-audio-controls.c). */
constexpr double logOffsetDb = 6.0;
constexpr double logRangeDb = 96.0;
const double logOffsetValue = -std::log10(logOffsetDb);
const double logRangeValue = -std::log10(logRangeDb + logOffsetDb);

int Clamp(int value, int maximum)
{
	return std::clamp(value, 0, maximum);
}

bool IsExtreme(int value)
{
	return value == 0 || value == 127;
}

bool InRange(int value, int low, int high)
{
	return value >= low && value <= high;
}
} // namespace

Message ParseShortMessage(uint32_t packed)
{
	Message message;
	int status = static_cast<int>(packed & 0xFF);
	int data1 = static_cast<int>((packed >> 8) & 0x7F);
	int data2 = static_cast<int>((packed >> 16) & 0x7F);

	message.channel = status & 0x0F;
	switch (status & 0xF0) {
	case 0x80:
		message.type = MessageType::NoteOff;
		message.number = data1;
		message.value = data2;
		break;
	case 0x90:
		message.type = MessageType::NoteOn;
		message.number = data1;
		message.value = data2;
		break;
	case 0xB0:
		message.type = MessageType::ControlChange;
		message.number = data1;
		message.value = data2;
		break;
	case 0xE0:
		message.type = MessageType::PitchBend;
		message.value = data1 | data2 << 7;
		break;
	default:
		return {};
	}
	return message;
}

uint32_t PackShortMessage(const Message &message)
{
	uint32_t channel = static_cast<uint32_t>(message.channel & 0x0F);
	uint32_t number = static_cast<uint32_t>(Clamp(message.number, 127));

	switch (message.type) {
	case MessageType::NoteOff:
		return (0x80 | channel) | number << 8 | static_cast<uint32_t>(Clamp(message.value, 127)) << 16;
	case MessageType::NoteOn:
		return (0x90 | channel) | number << 8 | static_cast<uint32_t>(Clamp(message.value, 127)) << 16;
	case MessageType::ControlChange:
		return (0xB0 | channel) | number << 8 | static_cast<uint32_t>(Clamp(message.value, 127)) << 16;
	case MessageType::PitchBend: {
		uint32_t value = static_cast<uint32_t>(Clamp(message.value, 16383));
		return (0xE0 | channel) | (value & 0x7F) << 8 | (value >> 7) << 16;
	}
	case MessageType::Invalid:
		break;
	}
	return 0;
}

bool Control::operator==(const Control &other) const
{
	return port == other.port && type == other.type && channel == other.channel && number == other.number;
}

bool Control::operator<(const Control &other) const
{
	return std::tie(port, type, channel, number) < std::tie(other.port, other.type, other.channel, other.number);
}

Control ControlOf(const QString &port, const Message &message)
{
	Control control;
	control.port = port;
	control.channel = message.channel;
	control.number = message.number;

	switch (message.type) {
	case MessageType::NoteOff:
	case MessageType::NoteOn:
		control.type = ControlType::Note;
		break;
	case MessageType::ControlChange:
		control.type = ControlType::ControlChange;
		break;
	case MessageType::PitchBend:
		control.type = ControlType::PitchBend;
		control.number = 0;
		break;
	case MessageType::Invalid:
		return {};
	}
	return control;
}

int MaximumValue(ControlType type)
{
	return type == ControlType::PitchBend ? 16383 : 127;
}

bool IsPress(const Message &message)
{
	switch (message.type) {
	case MessageType::NoteOn:
		return message.value > 0;
	case MessageType::ControlChange:
		return message.value >= 64;
	default:
		return false;
	}
}

double Normalized(const Message &message)
{
	int maximum = message.type == MessageType::PitchBend ? 16383 : 127;
	return std::clamp(static_cast<double>(message.value) / maximum, 0.0, 1.0);
}

Message MessageFor(const Control &control, int value)
{
	Message message;
	message.channel = control.channel;
	message.number = control.number;
	message.value = Clamp(value, MaximumValue(control.type));

	switch (control.type) {
	case ControlType::Note:
		message.type = MessageType::NoteOn;
		break;
	case ControlType::ControlChange:
		message.type = MessageType::ControlChange;
		break;
	case ControlType::PitchBend:
		message.type = MessageType::PitchBend;
		message.number = 0;
		break;
	case ControlType::Invalid:
		return {};
	}
	return message;
}

int ValueFromNormalized(ControlType type, double value)
{
	int maximum = MaximumValue(type);
	return Clamp(static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * maximum)), maximum);
}

QString NoteName(int note, const QStringList &names)
{
	if (names.size() != noteNamesPerOctave || note < 0 || note > 127) {
		return QString::number(note);
	}
	return names[note % noteNamesPerOctave] + QString::number(note / noteNamesPerOctave + lowestOctave);
}

int RelativeSteps(EncoderMode mode, int value)
{
	value = Clamp(value, 127);

	switch (mode) {
	case EncoderMode::TwosComplement:
		return value < 64 ? value : value - 128;
	case EncoderMode::BinaryOffset:
		return value - 64;
	case EncoderMode::SignMagnitude:
		return value < 64 ? value : -(value - 64);
	case EncoderMode::Absolute:
	case EncoderMode::Invalid:
		break;
	}
	return 0;
}

void GestureClassifier::Add(const Message &message)
{
	if (message.IsValid()) {
		messages.push_back(message);
	}
}

int GestureClassifier::LongestRepeat() const
{
	int longest = 0;
	int run = 0;
	for (size_t i = 0; i < messages.size(); i++) {
		run = i > 0 && messages[i].value == messages[i - 1].value ? run + 1 : 1;
		longest = std::max(longest, run);
	}
	return longest;
}

Gesture GestureClassifier::Result() const
{
	if (messages.empty()) {
		return Gesture::Invalid;
	}

	switch (messages.front().type) {
	case MessageType::NoteOff:
	case MessageType::NoteOn:
		return Gesture::Button;
	case MessageType::PitchBend:
		return Gesture::Continuous;
	case MessageType::ControlChange:
		break;
	case MessageType::Invalid:
		return Gesture::Invalid;
	}

	/* A button sends 127 when pressed and 0 when released. A knob sends
	 * other values too, except at its ends, and an encoder turned
	 * counterclockwise sends 127 over and over. */
	bool onlyExtremes = std::all_of(messages.begin(), messages.end(),
					[](const Message &message) { return IsExtreme(message.value); });
	if (!onlyExtremes || LongestRepeat() >= 3) {
		return Gesture::Continuous;
	}
	return Gesture::Button;
}

EncoderMode GestureClassifier::Encoder() const
{
	if (Result() != Gesture::Continuous || messages.front().type != MessageType::ControlChange) {
		return EncoderMode::Absolute;
	}

	/* A knob sends its position only when it changes; an encoder sends the
	 * same small step again for each notch. */
	if (LongestRepeat() < 2) {
		return EncoderMode::Absolute;
	}

	auto any = [this](int low, int high) {
		return std::any_of(messages.begin(), messages.end(),
				   [low, high](const Message &message) { return InRange(message.value, low, high); });
	};

	if (any(113, 127)) {
		return EncoderMode::TwosComplement;
	}
	if (any(57, 63)) {
		return EncoderMode::BinaryOffset;
	}
	if (any(65, 79)) {
		/* Small steps both ways only in sign and magnitude. */
		return any(1, 15) ? EncoderMode::SignMagnitude : EncoderMode::BinaryOffset;
	}
	return EncoderMode::TwosComplement;
}

bool Takeover::Accept(double incoming, double current)
{
	if (engaged && lastApplied >= 0.0 && std::abs(current - lastApplied) > tolerance) {
		engaged = false;
		lastIncoming = -1.0;
	}

	if (!engaged) {
		bool reached = std::abs(incoming - current) <= tolerance;
		bool crossed = lastIncoming >= 0.0 && (lastIncoming - current) * (incoming - current) < 0.0;
		engaged = reached || crossed;
	}

	lastIncoming = incoming;
	return engaged;
}

void Takeover::Reset()
{
	engaged = false;
	lastIncoming = -1.0;
	lastApplied = -1.0;
}

double VolumeDbToPosition(double db)
{
	if (db >= 0.0) {
		return 1.0;
	}
	if (db <= -logRangeDb) {
		return 0.0;
	}
	return (-std::log10(-db + logOffsetDb) - logRangeValue) / (logOffsetValue - logRangeValue);
}

double PositionToVolumeDb(double position)
{
	if (position >= 1.0) {
		return 0.0;
	}
	if (position <= 0.0) {
		return -std::numeric_limits<double>::infinity();
	}
	return -(logRangeDb + logOffsetDb) * std::pow((logRangeDb + logOffsetDb) / logOffsetDb, -position) +
	       logOffsetDb;
}

} // namespace OBS::Accessibility::Midi
