#pragma once

#include "StreamDeckMapping.hpp"
#include "StreamDeckProtocol.hpp"

#include <obs.h>

#include <QString>

#include <vector>

/* The OBS actions a Stream Deck key can run: every hotkey OBS has registered,
 * the ones listed in Settings > Hotkeys. */
namespace OBS::Accessibility::StreamDeck {

enum class ActionCategory {
	Invalid = 0,
	General = 1,
	Scenes = 2,
	Sources = 3,
	Outputs = 4,
	Encoders = 5,
	Services = 6,
};

struct ActionInfo {
	/* Never set to toggle; the caller decides that. */
	HotkeyRef ref;
	ActionCategory category = ActionCategory::Invalid;
	/* As Settings > Hotkeys shows it, with the source's name in front for
	 * a source's hotkey. */
	QString description;
	/* The other half of a pair, or empty. */
	QString partnerDescription;
};

/* Sorted by category, then in the order OBS registered them. */
std::vector<ActionInfo> ListActions();

QString CategoryName(ActionCategory category);

/* The hotkey a reference points to in this session, and with toggle set, the
 * other half of its pair. Either is OBS_INVALID_HOTKEY_ID when missing. */
struct ResolvedAction {
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id partner = OBS_INVALID_HOTKEY_ID;

	bool IsValid() const { return id != OBS_INVALID_HOTKEY_ID; }
};

ResolvedAction Resolve(const HotkeyRef &ref);

/* What an assignment does, in words, also when its source no longer exists:
 * "Start Streaming / Stop Streaming" for a toggle. */
QString Describe(const HotkeyRef &ref);

/* What to say after a key has run an action, for screen reader users who
 * cannot see the result: the new state when it can be read (a source muted, an
 * item hidden), otherwise the key's label. Empty when MetaOBS announces the
 * result anyway (streaming, recording, scenes, readouts) or when speaking
 * would get in the way (push-to-talk). */
QString Feedback(const HotkeyRef &ref, const QString &label);

/* Must be called on the UI thread, where the main window runs hotkeys. */
void Trigger(obs_hotkey_id id, bool pressed);
/* Runs whichever half of a pair applies, the way OBS does when both halves
 * share a key. */
void Toggle(const ResolvedAction &action);

/* "Key 1, top left". */
QString KeyName(const Model &model, int key);

} // namespace OBS::Accessibility::StreamDeck
