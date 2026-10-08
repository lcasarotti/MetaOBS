#pragma once

#include <obs.hpp>

class OBSBasic;
class QWidget;

/* Dialogs for the transition settings that the main window offers as menus
 * with a duration spin box inside, which screen readers cannot reach. */
namespace OBS::Accessibility {

/* The transition used when the scene item is shown (visible) or hidden, and
 * its duration, with undo. */
void EditVisibilityTransition(OBSBasic *main, obs_sceneitem_t *item, bool visible, QWidget *parent);

/* The transition used when switching to the scene instead of the current
 * one, and its duration. */
void EditSceneTransitionOverride(obs_source_t *sceneSource, QWidget *parent);

} // namespace OBS::Accessibility
