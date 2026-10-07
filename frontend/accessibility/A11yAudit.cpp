#include "A11yAudit.hpp"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollBar>

namespace OBS::Accessibility {

namespace {
constexpr int maxAuditedItems = 500;

QString StripMnemonic(QString text)
{
	text.remove(QRegularExpression("&(?!&)"));
	return text.trimmed();
}

bool IsInteractiveRole(QAccessible::Role role)
{
	switch (role) {
	case QAccessible::PushButton:
	case QAccessible::CheckBox:
	case QAccessible::RadioButton:
	case QAccessible::ComboBox:
	case QAccessible::SpinBox:
	case QAccessible::Slider:
	case QAccessible::Dial:
	case QAccessible::EditableText:
	case QAccessible::List:
	case QAccessible::Tree:
	case QAccessible::Table:
	case QAccessible::ButtonMenu:
	case QAccessible::ButtonDropDown:
	case QAccessible::PageTabList:
		return true;
	default:
		return false;
	}
}

bool HasValueRole(QAccessible::Role role)
{
	return role == QAccessible::Slider || role == QAccessible::SpinBox || role == QAccessible::Dial;
}

/* Line edits inside spin boxes and combo boxes are reported through their
 * owner, and scroll bars are not something users need to name. */
bool IsInternalWidget(const QWidget *widget)
{
	if (qobject_cast<const QScrollBar *>(widget)) {
		return true;
	}

	const QWidget *parent = widget->parentWidget();
	return qobject_cast<const QAbstractSpinBox *>(parent) || qobject_cast<const QComboBox *>(parent);
}

bool LooksLikeLocaleKey(const QString &text)
{
	static const QRegularExpression localeKey("^[A-Z][A-Za-z0-9]*(\\.[A-Z0-9][A-Za-z0-9]*)+$");
	return localeKey.match(text).hasMatch();
}

QString FindFormLabel(const QWidget *widget)
{
	for (const QWidget *current = widget; current && current->parentWidget();
	     current = current->parentWidget()) {
		QFormLayout *layout = qobject_cast<QFormLayout *>(current->parentWidget()->layout());
		if (!layout) {
			continue;
		}

		int row = -1;
		QFormLayout::ItemRole role;
		layout->getWidgetPosition(const_cast<QWidget *>(current), &row, &role);

		if (row < 0) {
			for (int i = 0; i < layout->rowCount() && row < 0; i++) {
				QLayoutItem *field = layout->itemAt(i, QFormLayout::FieldRole);
				QLayout *fieldLayout = field ? field->layout() : nullptr;
				if (fieldLayout && fieldLayout->indexOf(const_cast<QWidget *>(current)) >= 0) {
					row = i;
				}
			}
		}

		if (row >= 0) {
			QLayoutItem *labelItem = layout->itemAt(row, QFormLayout::LabelRole);
			QWidget *labelWidget = labelItem ? labelItem->widget() : nullptr;

			if (QLabel *label = qobject_cast<QLabel *>(labelWidget)) {
				return StripMnemonic(label->text());
			}
			if (labelWidget) {
				if (QLabel *label = labelWidget->findChild<QLabel *>()) {
					return StripMnemonic(label->text());
				}
			}
		}
	}

	return {};
}

QString DescribeWidget(const QWidget *widget)
{
	QString description = widget->metaObject()->className();

	if (!widget->objectName().isEmpty()) {
		description += QString(" \"%1\"").arg(widget->objectName());
	}

	QString label = FindFormLabel(widget);
	if (!label.isEmpty()) {
		description += QString(" (label \"%1\")").arg(label);
	}

	return description;
}

const QGroupBox *FindEnclosingGroupBox(const QWidget *widget)
{
	for (const QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
		if (const QGroupBox *groupBox = qobject_cast<const QGroupBox *>(parent)) {
			return groupBox;
		}
	}

	return nullptr;
}

void AuditItems(QAbstractItemView *view, std::vector<AuditIssue> &issues)
{
	QAbstractItemModel *model = view->model();
	if (!model) {
		return;
	}

	int audited = 0;
	int emptyItems = 0;
	std::vector<QModelIndex> pending = {QModelIndex()};

	while (!pending.empty() && audited < maxAuditedItems) {
		QModelIndex parent = pending.back();
		pending.pop_back();

		for (int row = 0; row < model->rowCount(parent) && audited < maxAuditedItems; row++) {
			QModelIndex index = model->index(row, 0, parent);
			audited++;

			QString text = model->data(index, Qt::AccessibleTextRole).toString().trimmed();
			if (text.isEmpty()) {
				text = model->data(index, Qt::DisplayRole).toString().trimmed();
			}
			if (text.isEmpty()) {
				emptyItems++;
			}

			if (model->hasChildren(index)) {
				pending.push_back(index);
			}
		}
	}

	if (emptyItems > 0) {
		issues.push_back({"empty-item", DescribeWidget(view),
				  QString("%1 of %2 items have no text").arg(emptyItems).arg(audited)});
	}
}

void AuditWidget(QWidget *widget, const AuditOptions &options, std::vector<AuditIssue> &issues)
{
	QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(widget);
	if (!iface || !iface->isValid()) {
		return;
	}

	QAccessible::Role role = iface->role();
	QString name = iface->text(QAccessible::Name).trimmed();
	QString description = iface->text(QAccessible::Description).trimmed();
	bool focusable = (widget->focusPolicy() & Qt::TabFocus) == Qt::TabFocus;

	if (const QGroupBox *groupBox = qobject_cast<const QGroupBox *>(widget)) {
		QString title = StripMnemonic(groupBox->title());
		if (!title.isEmpty() && name != title) {
			issues.push_back({"group-title-mismatch", DescribeWidget(widget),
					  QString("name \"%1\", title \"%2\"").arg(name, title)});
		}
	} else if (IsInteractiveRole(role) && focusable) {
		if (name.isEmpty()) {
			issues.push_back({"missing-name", DescribeWidget(widget), "focusable control without a name"});
		} else if (const QGroupBox *groupBox = FindEnclosingGroupBox(widget)) {
			if (name == StripMnemonic(groupBox->title())) {
				issues.push_back({"name-from-group", DescribeWidget(widget),
						  QString("name is the group title \"%1\"").arg(name)});
			}
		}
	}

	if (HasValueRole(role) && !iface->valueInterface()) {
		issues.push_back({"missing-value", DescribeWidget(widget), "no value interface"});
	}

	if (options.checkUntranslatedKeys) {
		for (const QString &text : {name, description}) {
			if (LooksLikeLocaleKey(text)) {
				issues.push_back({"untranslated-key", DescribeWidget(widget), text});
			}
		}
	}

	if (QAbstractItemView *view = qobject_cast<QAbstractItemView *>(widget)) {
		AuditItems(view, issues);
	}
}
} // namespace

std::vector<AuditIssue> AuditWidgetTree(QWidget *root, const AuditOptions &options)
{
	std::vector<AuditIssue> issues;
	if (!root) {
		return issues;
	}

	QList<QWidget *> widgets = root->findChildren<QWidget *>();
	widgets.prepend(root);

	for (QWidget *widget : widgets) {
		if (widget != root && !widget->isVisibleTo(root)) {
			continue;
		}
		if (IsInternalWidget(widget)) {
			continue;
		}

		AuditWidget(widget, options, issues);
	}

	return issues;
}

QString FormatIssues(const std::vector<AuditIssue> &issues)
{
	QString text;
	for (const AuditIssue &issue : issues) {
		text += QString("[%1] %2: %3\n").arg(issue.rule, issue.widget, issue.detail);
	}
	return text;
}

} // namespace OBS::Accessibility
