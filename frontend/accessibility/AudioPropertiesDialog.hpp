#pragma once

#include <obs.hpp>

#include <QDialog>

class OBSBasic;

namespace OBS::Accessibility {

/* The audio settings of one source, with standard controls: volume, mute,
 * monitoring, balance, sync offset, mono and tracks. The Advanced Audio
 * Properties dialog has the same settings for all sources in one grid, which
 * is slow to explore with a screen reader. Changes apply at once and can be
 * undone. */
class AudioPropertiesDialog : public QDialog {
	Q_OBJECT

public:
	static void Open(OBSBasic *main, obs_source_t *source, QWidget *parent);

private:
	AudioPropertiesDialog(OBSBasic *main, obs_source_t *source, QWidget *parent);

	OBSBasic *main;
	OBSWeakSource weakSource;
};

} // namespace OBS::Accessibility
