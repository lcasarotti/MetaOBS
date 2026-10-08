#pragma once

namespace OBS::Accessibility {

enum class AnnounceLevel {
	Off = 0,
	Essential = 1,
	Normal = 2,
	Verbose = 3,
};

enum class SpeechBackend {
	Automatic = 0,
	UIAutomation = 1,
	NvdaController = 2,
};

/* Accessibility preferences live in the [Accessibility] section of the user
 * config, next to the existing color override keys. */
namespace Settings {

constexpr int defaultFrameLossThreshold = 5;
constexpr int minimumFrameLossThreshold = 1;
constexpr int maximumFrameLossThreshold = 50;

void SetDefaults();

bool GetAccessibleMode();
void SetAccessibleMode(bool enabled);

AnnounceLevel GetAnnounceLevel();
void SetAnnounceLevel(AnnounceLevel level);

bool GetAnnounceOutputs();
void SetAnnounceOutputs(bool enabled);

bool GetAnnounceStatusMessages();
void SetAnnounceStatusMessages(bool enabled);

/* Warnings about lost frames and low disk space. */
bool GetAnnounceWarnings();
void SetAnnounceWarnings(bool enabled);

/* Share of lost frames, in percent, above which a warning is spoken. */
int GetFrameLossThreshold();
void SetFrameLossThreshold(int percent);

SpeechBackend GetSpeechBackend();
void SetSpeechBackend(SpeechBackend backend);

} // namespace Settings
} // namespace OBS::Accessibility
