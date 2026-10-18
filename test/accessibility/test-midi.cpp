/* Checks how MetaOBS reads MIDI messages, tells buttons from knobs, keeps
 * faders from making values jump, and saves the assignments. */

#include "a11y-test.hpp"

#include <accessibility/MidiMapping.hpp>
#include <accessibility/MidiProtocol.hpp>

#include <cmath>

namespace Midi = OBS::Accessibility::Midi;

namespace {
Midi::Message CC(int number, int value, int channel = 0)
{
	return {Midi::MessageType::ControlChange, channel, number, value};
}

Midi::Message NoteOn(int note, int velocity, int channel = 0)
{
	return {Midi::MessageType::NoteOn, channel, note, velocity};
}

bool Near(double a, double b, double tolerance = 1e-6)
{
	return std::abs(a - b) <= tolerance;
}

Midi::Assignment VolumeAssignment()
{
	Midi::Assignment assignment;
	assignment.control = {"Launch Control XL", Midi::ControlType::ControlChange, 8, 77};
	assignment.target.kind = Midi::TargetKind::SourceVolume;
	assignment.target.uuid = "1234";
	assignment.target.name = "Microphone";
	return assignment;
}
} // namespace

A11Y_TEST(MidiShortMessagesAreParsed)
{
	Midi::Message cc = Midi::ParseShortMessage(0x00400BB2);
	A11Y_CHECK(cc.type == Midi::MessageType::ControlChange);
	A11Y_CHECK(cc.channel == 2 && cc.number == 0x0B && cc.value == 0x40);

	Midi::Message note = Midi::ParseShortMessage(0x007F3C99);
	A11Y_CHECK(note.type == Midi::MessageType::NoteOn && note.channel == 9);
	A11Y_CHECK(note.number == 60 && note.value == 127);

	Midi::Message bend = Midi::ParseShortMessage(0x007F7FE0);
	A11Y_CHECK(bend.type == Midi::MessageType::PitchBend && bend.value == 16383);

	/* Program change and clock are not used. */
	A11Y_CHECK(!Midi::ParseShortMessage(0x000005C0).IsValid());
	A11Y_CHECK(!Midi::ParseShortMessage(0x000000F8).IsValid());

	for (const Midi::Message &message : {cc, note, bend}) {
		Midi::Message again = Midi::ParseShortMessage(Midi::PackShortMessage(message));
		A11Y_CHECK(again.type == message.type && again.channel == message.channel &&
			   again.number == message.number && again.value == message.value);
	}
}

A11Y_TEST(MidiPressesAndReleasesAreTold)
{
	A11Y_CHECK(Midi::IsPress(NoteOn(36, 100)));
	A11Y_CHECK(!Midi::IsPress(NoteOn(36, 0)));
	A11Y_CHECK(!Midi::IsPress({Midi::MessageType::NoteOff, 0, 36, 64}));
	A11Y_CHECK(Midi::IsPress(CC(20, 127)));
	A11Y_CHECK(!Midi::IsPress(CC(20, 0)));

	/* Note on and note off belong to the same key. */
	Midi::Control on = Midi::ControlOf("port", NoteOn(36, 100));
	Midi::Control off = Midi::ControlOf("port", {Midi::MessageType::NoteOff, 0, 36, 0});
	A11Y_CHECK(on == off && on.type == Midi::ControlType::Note);
	A11Y_CHECK(Midi::ControlOf("other", NoteOn(36, 100)) != on);
}

A11Y_TEST(MidiNotesAreNamedFromMiddleC)
{
	QStringList names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	A11Y_CHECK(Midi::NoteName(60, names) == "C3");
	A11Y_CHECK(Midi::NoteName(61, names) == "C#3");
	A11Y_CHECK(Midi::NoteName(0, names) == "C-2");
	A11Y_CHECK(Midi::NoteName(127, names) == "G8");
	A11Y_CHECK(Midi::NoteName(60, {"C"}) == "60");
}

A11Y_TEST(MidiEncoderStepsAreDecoded)
{
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::TwosComplement, 1) == 1);
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::TwosComplement, 127) == -1);
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::TwosComplement, 125) == -3);
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::BinaryOffset, 65) == 1);
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::BinaryOffset, 62) == -2);
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::SignMagnitude, 2) == 2);
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::SignMagnitude, 66) == -2);
	A11Y_CHECK(Midi::RelativeSteps(Midi::EncoderMode::Absolute, 66) == 0);
}

A11Y_TEST(MidiButtonsAreToldFromKnobs)
{
	Midi::GestureClassifier classifier;
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Invalid);

	classifier.Add(NoteOn(36, 90));
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Button);

	/* A button that sends a controller: pressed, then released. */
	classifier.Clear();
	classifier.Add(CC(20, 127));
	classifier.Add(CC(20, 0));
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Button);

	classifier.Clear();
	classifier.Add(CC(77, 40));
	classifier.Add(CC(77, 41));
	classifier.Add(CC(77, 43));
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Continuous);
	A11Y_CHECK(classifier.Encoder() == Midi::EncoderMode::Absolute);

	/* A fader pushed to the top in one go. */
	classifier.Clear();
	classifier.Add(CC(77, 127));
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Button);
	classifier.Add(CC(77, 126));
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Continuous);

	classifier.Clear();
	classifier.Add({Midi::MessageType::PitchBend, 0, 0, 9000});
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Continuous);
}

A11Y_TEST(MidiEndlessEncodersAreRecognized)
{
	Midi::GestureClassifier classifier;

	/* Turned counterclockwise in two's complement: 127 over and over. */
	for (int i = 0; i < 4; i++) {
		classifier.Add(CC(16, 127));
	}
	A11Y_CHECK(classifier.Result() == Midi::Gesture::Continuous);
	A11Y_CHECK(classifier.Encoder() == Midi::EncoderMode::TwosComplement);

	classifier.Clear();
	for (int value : {1, 1, 2, 1}) {
		classifier.Add(CC(16, value));
	}
	A11Y_CHECK(classifier.Encoder() == Midi::EncoderMode::TwosComplement);

	classifier.Clear();
	for (int value : {63, 63, 62}) {
		classifier.Add(CC(16, value));
	}
	A11Y_CHECK(classifier.Encoder() == Midi::EncoderMode::BinaryOffset);

	classifier.Clear();
	for (int value : {1, 1, 65, 65}) {
		classifier.Add(CC(16, value));
	}
	A11Y_CHECK(classifier.Encoder() == Midi::EncoderMode::SignMagnitude);
}

A11Y_TEST(MidiTakeoverWaitsForTheFader)
{
	Midi::Takeover takeover;

	/* The fader is low while the value is at 0.5. */
	A11Y_CHECK(!takeover.Accept(0.10, 0.5));
	A11Y_CHECK(!takeover.Accept(0.30, 0.5));
	/* Crossing the value takes over. */
	A11Y_CHECK(takeover.Accept(0.55, 0.5));
	takeover.Applied(0.55);
	A11Y_CHECK(takeover.Accept(0.60, 0.55));
	takeover.Applied(0.60);

	/* The value changed elsewhere: the fader has to catch up again. */
	A11Y_CHECK(!takeover.Accept(0.62, 0.20));
	A11Y_CHECK(takeover.Accept(0.21, 0.20));

	takeover.Reset();
	A11Y_CHECK(takeover.Accept(0.50, 0.505));
}

A11Y_TEST(MidiVolumeCurveMatchesTheMixer)
{
	A11Y_CHECK(Near(Midi::VolumeDbToPosition(0.0), 1.0));
	A11Y_CHECK(Near(Midi::VolumeDbToPosition(-96.0), 0.0));
	A11Y_CHECK(Near(Midi::PositionToVolumeDb(1.0), 0.0));
	A11Y_CHECK(std::isinf(Midi::PositionToVolumeDb(0.0)));

	for (double db : {-60.0, -30.0, -12.0, -6.0, -1.0}) {
		A11Y_CHECK(Near(Midi::PositionToVolumeDb(Midi::VolumeDbToPosition(db)), db, 1e-6));
	}
	/* Positions grow with the volume. */
	A11Y_CHECK(Midi::VolumeDbToPosition(-30.0) < Midi::VolumeDbToPosition(-12.0));
}

A11Y_TEST(MidiAssignmentsSurviveSaving)
{
	Midi::Assignment volume = VolumeAssignment();
	volume.options.takeover = false;
	volume.options.low = 0.25;

	Midi::Assignment hotkey;
	hotkey.control = {"Launch Control XL", Midi::ControlType::Note, 8, 41};
	hotkey.target.kind = Midi::TargetKind::Hotkey;
	hotkey.target.hotkey.owner = OBS::Accessibility::StreamDeck::HotkeyOwner::Frontend;
	hotkey.target.hotkey.name = "OBSBasic.StartStreaming";
	hotkey.target.hotkey.toggle = true;
	hotkey.options.onValue = 60;

	Midi::Assignment property;
	property.control = {"Launch Control XL", Midi::ControlType::ControlChange, 8, 13};
	property.target.kind = Midi::TargetKind::Property;
	property.target.uuid = "5678";
	property.target.name = "Gain";
	property.target.parentName = "Microphone";
	property.target.setting = "db";
	property.target.settingLabel = "Gain";
	property.target.propertyType = Midi::PropertyType::Float;
	property.target.minimum = -30.0;
	property.target.maximum = 30.0;
	property.target.step = 0.1;
	property.target.suffix = " dB";
	property.options.encoder = Midi::EncoderMode::TwosComplement;

	Midi::Mapping mapping = Midi::MappingFromJson(Midi::MappingToJson({volume, hotkey, property}));
	A11Y_CHECK(mapping.size() == 3);
	if (mapping.size() != 3) {
		return;
	}

	A11Y_CHECK(mapping[0].control == volume.control);
	A11Y_CHECK(mapping[0].target.SameAs(volume.target) && mapping[0].target.name == "Microphone");
	A11Y_CHECK(!mapping[0].options.takeover && Near(mapping[0].options.low, 0.25));

	A11Y_CHECK(mapping[1].target.hotkey.name == "OBSBasic.StartStreaming" && mapping[1].target.hotkey.toggle);
	A11Y_CHECK(mapping[1].options.onValue == 60);

	const Midi::Target &target = mapping[2].target;
	A11Y_CHECK(target.SameAs(property.target) && target.parentName == "Microphone");
	A11Y_CHECK(target.propertyType == Midi::PropertyType::Float && Near(target.minimum, -30.0));
	A11Y_CHECK(target.suffix == " dB" && target.IsContinuous());
	A11Y_CHECK(mapping[2].options.encoder == Midi::EncoderMode::TwosComplement);
}

A11Y_TEST(MidiDamagedAssignmentsAreSkipped)
{
	A11Y_CHECK(Midi::MappingFromJson("not json").empty());

	QString json = R"({"version":1,"assignments":[
		{"port":"A","type":"cc","channel":0,"number":7,"target":{"kind":"teleport"}},
		{"port":"A","type":"cc","channel":99,"number":7,"target":{"kind":"tbar"}},
		{"port":"","type":"cc","channel":0,"number":7,"target":{"kind":"tbar"}},
		{"port":"A","type":"cc","channel":0,"number":7,"target":{"kind":"tbar"}},
		{"port":"A","type":"cc","channel":0,"number":7,"target":{"kind":"transitionDuration"}}
	]})";
	Midi::Mapping mapping = Midi::MappingFromJson(json);
	/* Only the first valid entry for each control is kept. */
	A11Y_CHECK(mapping.size() == 1);
	A11Y_CHECK(!mapping.empty() && mapping[0].target.kind == Midi::TargetKind::TBar);
}

A11Y_TEST(MidiAssignmentsAreFoundByControlAndTarget)
{
	Midi::Mapping mapping = {VolumeAssignment()};
	A11Y_CHECK(Midi::FindControl(mapping, VolumeAssignment().control) == 0);

	Midi::Target other = VolumeAssignment().target;
	A11Y_CHECK(Midi::FindTarget(mapping, other) == 0);
	other.kind = Midi::TargetKind::SourceMute;
	A11Y_CHECK(Midi::FindTarget(mapping, other) < 0);
}

A11Y_TEST(MidiOutputPortsArePairedWithInputs)
{
	std::vector<QString> outputs = {"Microsoft GS Wavetable Synth", "Launch Control XL",
					"MIDIOUT2 (Launch Control XL)"};
	A11Y_CHECK(Midi::PairedOutput("Launch Control XL", outputs) == "Launch Control XL");
	A11Y_CHECK(Midi::PairedOutput("MIDIIN2 (Launch Control XL)", outputs) == "MIDIOUT2 (Launch Control XL)");
	A11Y_CHECK(Midi::PairedOutput("Keystation", outputs).isEmpty());

	Midi::PortSettings settings;
	A11Y_CHECK(Midi::OutputFor(settings, "Launch Control XL", outputs) == "Launch Control XL");
	settings.outputs["Launch Control XL"] = "";
	A11Y_CHECK(Midi::OutputFor(settings, "Launch Control XL", outputs).isEmpty());

	settings.SetInputEnabled("MIDIIN2 (Launch Control XL)", false);
	Midi::PortSettings again = Midi::PortSettingsFromJson(Midi::PortSettingsToJson(settings));
	A11Y_CHECK(!again.IsInputEnabled("MIDIIN2 (Launch Control XL)"));
	A11Y_CHECK(again.IsInputEnabled("Launch Control XL"));
	A11Y_CHECK(again.outputs.count("Launch Control XL") == 1 && again.outputs["Launch Control XL"].isEmpty());
}
