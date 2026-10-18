#pragma once

#include "MidiProtocol.hpp"
#include "StreamDeckMapping.hpp"

#include <QString>

#include <map>
#include <vector>

/* What each MIDI control does, and how the assignments are saved. Kept free
 * of libobs so it can be tested. */
namespace OBS::Accessibility::Midi {

enum class TargetKind {
	Invalid = 0,
	/* Any OBS hotkey, as on the Stream Deck. */
	Hotkey = 1,
	SourceVolume = 2,
	SourceMute = 3,
	SourceMonitoring = 4,
	SourceBalance = 5,
	/* The T-bar of Studio Mode. */
	TBar = 6,
	TransitionDuration = 7,
	/* A setting of a source or filter, as its properties show it. */
	Property = 8,
};

enum class PropertyType {
	Invalid = 0,
	Bool = 1,
	Int = 2,
	Float = 3,
};

struct Target {
	TargetKind kind = TargetKind::Invalid;

	/* For Hotkey. */
	StreamDeck::HotkeyRef hotkey;

	/* For the source kinds and Property: the source's or filter's UUID,
	 * and its name when the assignment was made, to describe it while it
	 * is missing. For a filter, parentName is the name of its source. */
	QString uuid;
	QString name;
	QString parentName;

	/* For Property. */
	QString setting;
	QString settingLabel;
	PropertyType propertyType = PropertyType::Invalid;
	double minimum = 0.0;
	double maximum = 1.0;
	double step = 0.0;
	/* " dB", " %"... as the property shows it after the value. */
	QString suffix;

	bool IsValid() const;
	/* Whether a knob or fader sets it, rather than a button. */
	bool IsContinuous() const;
	bool SameAs(const Target &other) const;
};

struct Options {
	EncoderMode encoder = EncoderMode::Absolute;
	/* Whether a fader catches up with the value before it moves it. */
	bool takeover = true;
	bool invert = false;
	/* The part of the target's range the control covers, from 0 to 1. */
	double low = 0.0;
	double high = 1.0;
	/* Whether the target's state is sent back to the controller. */
	bool feedback = true;
	/* What lights a key's LED, and what turns it off. */
	int onValue = 127;
	int offValue = 0;
};

struct Assignment {
	Control control;
	Target target;
	Options options;
};

/* At most one assignment per control. */
using Mapping = std::vector<Assignment>;

/* Index of the control's assignment, or -1. */
int FindControl(const Mapping &mapping, const Control &control);
int FindTarget(const Mapping &mapping, const Target &target);

QString MappingToJson(const Mapping &mapping);
/* Skips entries it does not understand, so that a damaged setting loses as
 * little as possible. */
Mapping MappingFromJson(const QString &json);

/* Which input ports MetaOBS listens to, and where it sends each one's
 * feedback. Ports are found by name, since their numbers change when devices
 * come and go. */
struct PortSettings {
	/* Input ports the user turned off. All others are used. */
	std::vector<QString> disabledInputs;
	/* The output port chosen for an input port. An empty name turns
	 * feedback off; a missing entry picks one automatically. */
	std::map<QString, QString> outputs;

	bool IsInputEnabled(const QString &input) const;
	void SetInputEnabled(const QString &input, bool enabled);
};

QString PortSettingsToJson(const PortSettings &settings);
PortSettings PortSettingsFromJson(const QString &json);

/* The output port of the same device as an input port: the one with the same
 * name, or "MIDIOUT2 (Device)" for "MIDIIN2 (Device)". Empty when none
 * matches. */
QString PairedOutput(const QString &input, const std::vector<QString> &outputs);

/* The output port an input port's feedback goes to, following the settings. */
QString OutputFor(const PortSettings &settings, const QString &input, const std::vector<QString> &outputs);

} // namespace OBS::Accessibility::Midi
