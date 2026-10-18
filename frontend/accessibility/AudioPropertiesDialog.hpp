#pragma once

#include <obs.hpp>

#include <QDialog>

#include <vector>

class OBSBasic;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace OBS::Accessibility {

/* The audio settings of one source, with standard controls: volume, mute,
 * monitoring, balance, sync offset, mono and tracks. The Advanced Audio
 * Properties dialog has the same settings for all sources in one grid, which
 * is slow to explore with a screen reader. Changes apply at once and can be
 * undone. The controls follow changes made elsewhere, by a MIDI controller for
 * instance, and MIDI learn can assign the volume, mute, monitoring and
 * balance. */
class AudioPropertiesDialog : public QDialog {
	Q_OBJECT

public:
	static void Open(OBSBasic *main, obs_source_t *source, QWidget *parent);

private:
	AudioPropertiesDialog(OBSBasic *main, obs_source_t *source, QWidget *parent);

	static void OnSourceSignal(void *data, calldata_t *params);
	void Refresh();

	OBSBasic *main;
	OBSWeakSource weakSource;
	std::vector<OBSSignal> sourceSignals;

	QDoubleSpinBox *volume;
	QCheckBox *muted;
	QCheckBox *monitoring;
	QSpinBox *balance;
};

} // namespace OBS::Accessibility
