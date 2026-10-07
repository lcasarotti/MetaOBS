#pragma once

#include <QString>

#include <vector>

class QWidget;

namespace OBS::Accessibility {

struct AuditIssue {
	QString rule;
	QString widget;
	QString detail;
};

struct AuditOptions {
	/* Report accessible names that look like untranslated locale keys
	 * (e.g. "Basic.Settings.General"). Only meaningful when the
	 * application translations are loaded. */
	bool checkUntranslatedKeys = true;
};

/* Checks the accessibility information Qt exposes for every visible widget
 * below root, the same information NVDA receives through UI Automation.
 *
 * Rules:
 * - missing-name: a focusable control has no accessible name
 * - name-from-group: a control's only name is the title of its group box,
 *   which means its own label is not associated with it
 * - group-title-mismatch: a group box's name differs from its title
 * - missing-value: a slider or spin box exposes no value interface
 * - empty-item: an item in a list, tree or table has no text
 * - untranslated-key: a name looks like a locale key */
std::vector<AuditIssue> AuditWidgetTree(QWidget *root, const AuditOptions &options = {});

QString FormatIssues(const std::vector<AuditIssue> &issues);

} // namespace OBS::Accessibility
