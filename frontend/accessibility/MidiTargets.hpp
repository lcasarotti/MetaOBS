#pragma once

#include "MidiMapping.hpp"
#include "MidiWidgetProperties.hpp"

#include <obs.hpp>

#include <QString>

#include <vector>

class QWidget;

/* What MIDI controls act on in this session: finding the targets again,
 * reading and changing them, and describing them. Hotkeys and the T-bar are
 * run by MidiManager, which keeps their state. Must be used on the UI
 * thread. */
namespace OBS::Accessibility::Midi {

/* "CC 7, channel 1", with the port after it when asked. */
QString DescribeControl(const Control &control, bool withPort = false);
/* "Microphone: volume", also when the source no longer exists. */
QString DescribeTarget(const Target &target);
/* The target's current value or state in words: "Microphone: -12.5 dB". */
QString DescribeValue(const Target &target);

/* Whether the target exists in this session. */
bool IsAvailable(const Target &target);

/* A continuous target's value from 0 to 1, or 1 or 0 for a button target
 * that is on or off, hotkeys included. -1 when it cannot be read or has no
 * state. */
double ReadValue(const Target &target);
/* Sets a continuous target, from 0 to 1. Not for the T-bar. */
void ApplyValue(const Target &target, double value);
/* Turns a button target on or off, except hotkeys. */
void ToggleTarget(const Target &target);

/* How far one notch of an encoder moves a continuous target, from 0 to 1. */
double EncoderStep(const Target &target);

/* The source or filter a target acts on, if it exists. */
OBSSource FindSource(const Target &target);

/* The targets the widget stands for: at most one continuous target and one
 * button target, in that order. Empty when it stands for nothing MIDI can
 * control. */
std::vector<Target> TargetsForWidget(QWidget *widget);

} // namespace OBS::Accessibility::Midi
