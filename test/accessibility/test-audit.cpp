/* Checks that the audit rules catch the problems they are meant to catch, so a
 * clean audit of a real dialog can be trusted. */

#include "a11y-test.hpp"

#include <accessibility/A11yAudit.hpp>

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <iostream>

using OBS::Accessibility::AuditIssue;
using OBS::Accessibility::AuditWidgetTree;
using OBS::Accessibility::FormatIssues;

namespace {
bool HasRule(const std::vector<AuditIssue> &issues, const char *rule)
{
	return std::any_of(issues.begin(), issues.end(),
			   [rule](const AuditIssue &issue) { return issue.rule == rule; });
}

void PrintIssues(const std::vector<AuditIssue> &issues)
{
	if (!issues.empty()) {
		std::cout << FormatIssues(issues).toStdString();
	}
}
} // namespace

A11Y_TEST(AuditReportsUnlabeledLineEdit)
{
	QWidget root;
	QFormLayout *layout = new QFormLayout(&root);
	layout->addRow(new QLineEdit);

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(HasRule(issues, "missing-name"));
}

A11Y_TEST(AuditAcceptsFormRowWithLabel)
{
	QWidget root;
	QFormLayout *layout = new QFormLayout(&root);
	layout->addRow("Name", new QLineEdit);

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(issues.empty());
}

/* This is the pattern the properties view uses for numbers, paths, colors
 * and fonts: the label is attached to a layout, not to the control. */
A11Y_TEST(AuditReportsLabelAttachedToLayout)
{
	QWidget root;
	QFormLayout *layout = new QFormLayout(&root);
	QHBoxLayout *row = new QHBoxLayout;
	row->addWidget(new QSpinBox);
	layout->addRow(new QLabel("Width"), row);

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(HasRule(issues, "missing-name"));
}

A11Y_TEST(AuditReportsNameTakenFromGroupTitle)
{
	QWidget root;
	QVBoxLayout *rootLayout = new QVBoxLayout(&root);
	QGroupBox *groupBox = new QGroupBox("Audio");
	QVBoxLayout *groupLayout = new QVBoxLayout(groupBox);
	groupLayout->addWidget(new QSpinBox);
	rootLayout->addWidget(groupBox);

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(HasRule(issues, "name-from-group"));
}

A11Y_TEST(AuditReportsGroupNameDifferentFromTitle)
{
	QWidget root;
	QVBoxLayout *rootLayout = new QVBoxLayout(&root);
	QGroupBox *groupBox = new QGroupBox("Advanced");
	groupBox->setAccessibleName("group");
	rootLayout->addWidget(groupBox);

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(HasRule(issues, "group-title-mismatch"));
}

A11Y_TEST(AuditReportsButtonWithoutText)
{
	QWidget root;
	QVBoxLayout *layout = new QVBoxLayout(&root);
	layout->addWidget(new QPushButton);

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(HasRule(issues, "missing-name"));
}

A11Y_TEST(AuditReportsEmptyListItems)
{
	QWidget root;
	QVBoxLayout *layout = new QVBoxLayout(&root);
	QListWidget *list = new QListWidget;
	list->setAccessibleName("Filters");
	list->addItem(new QListWidgetItem());
	layout->addWidget(list);

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(HasRule(issues, "empty-item"));
}

A11Y_TEST(AuditAcceptsSliderWithValue)
{
	QWidget root;
	QFormLayout *layout = new QFormLayout(&root);
	layout->addRow("Volume", new QSlider(Qt::Horizontal));

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(issues.empty());
}

A11Y_TEST(AuditReportsUntranslatedKey)
{
	QWidget root;
	QVBoxLayout *layout = new QVBoxLayout(&root);
	layout->addWidget(new QPushButton("Basic.Settings.General"));

	auto issues = AuditWidgetTree(&root);
	PrintIssues(issues);
	A11Y_CHECK(HasRule(issues, "untranslated-key"));
}
