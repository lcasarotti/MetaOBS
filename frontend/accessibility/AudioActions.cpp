#include "AudioActions.hpp"
#include "AccessibleFormat.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <algorithm>
#include <string>

namespace OBS::Accessibility {

namespace {
bool GetPrivateBool(obs_source_t *source, const char *name)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(source);
	return obs_data_get_bool(settings, name);
}

void ApplyMono(obs_source_t *source, bool mono)
{
	uint32_t flags = obs_source_get_flags(source);
	flags = mono ? (flags | OBS_SOURCE_FLAG_FORCE_MONO) : (flags & ~OBS_SOURCE_FLAG_FORCE_MONO);
	obs_source_set_flags(source, flags);
}

/* Applies a value and adds an undo action that restores the previous one.
 * Repeatable actions merge with the previous action of the same source, so a
 * series of volume steps is undone at once, as with the mixer's sliders. */
template<typename T>
void ApplyWithUndo(OBSBasic *main, obs_source_t *source, const char *undoLookupKey, T previous, T value,
		   void (*apply)(obs_source_t *, T), bool repeatable)
{
	apply(source, value);

	auto undoRedo = [apply](const std::string &uuid, T newValue) {
		OBSSourceAutoRelease target = obs_get_source_by_uuid(uuid.c_str());
		if (target) {
			apply(target, newValue);
		}
	};

	std::string uuid = obs_source_get_uuid(source);
	main->undo_s.add_action(QTStr(undoLookupKey).arg(obs_source_get_name(source)),
				std::bind(undoRedo, std::placeholders::_1, previous),
				std::bind(undoRedo, std::placeholders::_1, value), uuid, uuid, repeatable);
}
} // namespace

bool IsShownInMixer(obs_source_t *source, bool studioModePreviewed)
{
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0 || !obs_source_audio_active(source)) {
		return false;
	}

	config_t *config = App()->GetUserConfig();
	bool showInactive = config_get_bool(config, "BasicWindow", "MixerShowInactive");
	bool showHidden = config_get_bool(config, "BasicWindow", "MixerShowHidden");

	bool pinned = GetPrivateBool(source, "mixer_pinned");
	bool hidden = GetPrivateBool(source, "mixer_hidden");
	bool active = obs_source_active(source);

	/* The same rules as the audio mixer. */
	if (pinned || (hidden && showHidden)) {
		return true;
	}

	return !hidden && (active || showInactive || studioModePreviewed);
}

float GetVolumeDb(obs_source_t *source)
{
	float volume = obs_source_get_volume(source);
	return volume > 0.0f ? obs_mul_to_db(volume) : minimumVolumeDb;
}

bool IsMono(obs_source_t *source)
{
	return (obs_source_get_flags(source) & OBS_SOURCE_FLAG_FORCE_MONO) != 0;
}

void SetVolumeDbWithUndo(OBSBasic *main, obs_source_t *source, float db)
{
	float volume = db <= minimumVolumeDb ? 0.0f : obs_db_to_mul(db);
	ApplyWithUndo<float>(main, source, "Undo.Volume.Change", obs_source_get_volume(source), volume,
			     obs_source_set_volume, true);
}

void SetMutedWithUndo(OBSBasic *main, obs_source_t *source, bool muted)
{
	ApplyWithUndo<bool>(main, source, muted ? "Undo.Volume.Mute" : "Undo.Volume.Unmute", obs_source_muted(source),
			    muted, obs_source_set_muted, false);
}

void SetMonitoringWithUndo(OBSBasic *main, obs_source_t *source, bool enabled)
{
	ApplyWithUndo<bool>(main, source, "Undo.MonitoringType.Change", obs_source_get_monitoring_enabled(source),
			    enabled, obs_source_set_monitoring_enabled, false);
}

void SetBalanceWithUndo(OBSBasic *main, obs_source_t *source, float balance)
{
	ApplyWithUndo<float>(main, source, "Undo.Balance.Change", obs_source_get_balance_value(source), balance,
			     obs_source_set_balance_value, true);
}

void SetSyncOffsetWithUndo(OBSBasic *main, obs_source_t *source, int64_t offsetNs)
{
	ApplyWithUndo<int64_t>(main, source, "Undo.SyncOffset.Change", obs_source_get_sync_offset(source), offsetNs,
			       obs_source_set_sync_offset, true);
}

void SetMonoWithUndo(OBSBasic *main, obs_source_t *source, bool mono)
{
	ApplyWithUndo<bool>(main, source, mono ? "Undo.ForceMono.On" : "Undo.ForceMono.Off", IsMono(source), mono,
			    ApplyMono, false);
}

void SetMixersWithUndo(OBSBasic *main, obs_source_t *source, uint32_t mixers)
{
	ApplyWithUndo<uint32_t>(main, source, "Undo.Mixers.Change", obs_source_get_audio_mixers(source), mixers,
				obs_source_set_audio_mixers, false);
}

QString DescribeAudioSource(obs_source_t *source)
{
	QString text = QT_UTF8(obs_source_get_name(source)) + ", " + FormatDb(GetVolumeDb(source));

	if (obs_source_muted(source)) {
		text += ", " + QTStr("Accessibility.Audio.Muted");
	}
	if (obs_source_get_monitoring_enabled(source)) {
		text += ", " + QTStr("Accessibility.Audio.Monitored");
	}
	if (!obs_source_active(source)) {
		text += ", " + QTStr("Accessibility.Audio.Inactive");
	}
	if (GetPrivateBool(source, "mixer_hidden")) {
		text += ", " + QTStr("Accessibility.State.Hidden");
	}

	return text;
}

} // namespace OBS::Accessibility
