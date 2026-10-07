#pragma once

class OBSBasic;

/* Entry points of the accessibility module. Kept to a handful of calls so the
 * hooks in upstream files stay small. */
namespace OBS::Accessibility {

/* Adds the accessibility translations to the application's text lookup.
 * Call right after the main locale has been loaded. */
void LoadLocale();

/* Sets up announcements. Call once the main window has been initialized. */
void Initialize(OBSBasic *main);

} // namespace OBS::Accessibility
