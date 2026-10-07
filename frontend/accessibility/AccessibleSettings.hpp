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

void SetDefaults();

AnnounceLevel GetAnnounceLevel();
void SetAnnounceLevel(AnnounceLevel level);

bool GetAnnounceOutputs();
void SetAnnounceOutputs(bool enabled);

bool GetAnnounceStatusMessages();
void SetAnnounceStatusMessages(bool enabled);

SpeechBackend GetSpeechBackend();
void SetSpeechBackend(SpeechBackend backend);

} // namespace Settings
} // namespace OBS::Accessibility
