#pragma once

#include <obs.hpp>

#include <QString>

class OBSBasic;

/* Audio changes made from the accessible window, each with an undo action like
 * the ones the mixer and the Advanced Audio Properties dialog add. */
namespace OBS::Accessibility {

/* Whether the source is shown in the main window's audio mixer. */
bool IsShownInMixer(obs_source_t *source, bool studioModePreviewed);

float GetVolumeDb(obs_source_t *source);
bool IsMono(obs_source_t *source);

void SetVolumeDbWithUndo(OBSBasic *main, obs_source_t *source, float db);
void SetMutedWithUndo(OBSBasic *main, obs_source_t *source, bool muted);
void SetMonitoringWithUndo(OBSBasic *main, obs_source_t *source, bool enabled);
void SetBalanceWithUndo(OBSBasic *main, obs_source_t *source, float balance);
void SetSyncOffsetWithUndo(OBSBasic *main, obs_source_t *source, int64_t offsetNs);
void SetMonoWithUndo(OBSBasic *main, obs_source_t *source, bool mono);
void SetMixersWithUndo(OBSBasic *main, obs_source_t *source, uint32_t mixers);

/* Name, volume, and muted or monitored state, for lists and announcements. */
QString DescribeAudioSource(obs_source_t *source);

} // namespace OBS::Accessibility
