#pragma once

/* Dynamic properties that tell MIDI learn what a widget stands for. Set on
 * all platforms, read only where MIDI is supported. */
namespace OBS::Accessibility::Midi {

/* "audioSources" on a list whose current item holds a source UUID in
 * Qt::UserRole, "scenes" on such a list of scenes; "tbar";
 * "transitionDuration"; "volume", "mute", "monitoring" or "balance" on a
 * widget that also has sourceProperty. */
constexpr const char *targetProperty = "metaobsMidiTarget";
/* The UUID of the source whose volume, mute... a widget sets. */
constexpr const char *sourceProperty = "metaobsMidiSource";
/* The name of a frontend hotkey a button runs. */
constexpr const char *hotkeyProperty = "metaobsHotkey";
/* Set by the properties view on the widgets of a setting. */
constexpr const char *settingProperty = "obsSettingName";

} // namespace OBS::Accessibility::Midi
