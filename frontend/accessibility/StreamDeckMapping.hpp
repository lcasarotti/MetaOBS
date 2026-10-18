#pragma once

#include <QString>

#include <map>

/* Which OBS action each Stream Deck key runs, and how the assignments are
 * saved. Kept free of libobs so it can be tested. */
namespace OBS::Accessibility::StreamDeck {

/* Who registered a hotkey. The values match obs_hotkey_registerer_type plus
 * one, so that 0 stays invalid. */
enum class HotkeyOwner {
	Invalid = 0,
	Frontend = 1,
	Source = 2,
	Output = 3,
	Encoder = 4,
	Service = 5,
};

/* An OBS hotkey, found again by name in later sessions: hotkey IDs change
 * every time OBS starts. */
struct HotkeyRef {
	HotkeyOwner owner = HotkeyOwner::Invalid;
	/* The source's UUID, or the output's, encoder's or service's name.
	 * Empty for frontend hotkeys. */
	QString context;
	/* The source's name when the assignment was made, to describe it
	 * while the source is missing. */
	QString contextName;
	QString name;
	/* For one half of a pair (Start and Stop Streaming, Mute and Unmute),
	 * run whichever half applies. */
	bool toggle = false;

	bool IsValid() const { return owner != HotkeyOwner::Invalid && !name.isEmpty(); }
};

/* How an owner is written in saved settings: "frontend", "source"... */
QString HotkeyOwnerName(HotkeyOwner owner);
HotkeyOwner HotkeyOwnerNamed(const QString &name);

struct KeyAssignment {
	HotkeyRef action;
	/* What the key says: spoken on request and shown on its display. */
	QString label;
};

/* Assignments by key index, from 0. */
using Mapping = std::map<int, KeyAssignment>;

QString MappingToJson(const Mapping &mapping);
/* Skips entries it does not understand, so that a damaged setting loses as
 * little as possible. */
Mapping MappingFromJson(const QString &json);

} // namespace OBS::Accessibility::StreamDeck
