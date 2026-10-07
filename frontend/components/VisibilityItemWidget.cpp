#include "VisibilityItemWidget.hpp"

#include <components/OBSSourceLabel.hpp>

#include <OBSApp.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QHBoxLayout>

#include "moc_VisibilityItemWidget.cpp"

VisibilityItemWidget::VisibilityItemWidget(obs_source_t *source_)
	: source(source_),
	  enabledSignal(obs_source_get_signal_handler(source), "enable", OBSSourceEnabled, this)
{
	bool enabled = obs_source_enabled(source);

	vis = new QCheckBox();
	vis->setProperty("class", "checkbox-icon indicator-visibility");
	vis->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	vis->setChecked(enabled);

	label = new OBSSourceLabel(source);
	label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	QHBoxLayout *itemLayout = new QHBoxLayout();
	itemLayout->addWidget(vis);
	itemLayout->addWidget(label);
	itemLayout->setContentsMargins(0, 0, 0, 0);

	setLayout(itemLayout);

	connect(vis, &QCheckBox::clicked, this, [this](bool visible) {
		obs_source_set_enabled(source, visible);
		const char *filterName = obs_source_get_name(source);
		obs_source_t *parent = obs_filter_get_parent(source);
		const char *sourceName = obs_source_get_name(parent);
		blog(LOG_INFO, "User set filter '%s' on source '%s' to %s", filterName, sourceName,
		     visible ? "enabled" : "disabled");
	});

	connect(vis, &QCheckBox::toggled, this, &VisibilityItemWidget::UpdateAccessibleText);
	connect(label, &OBSSourceLabel::renamed, this, &VisibilityItemWidget::UpdateAccessibleText,
		Qt::QueuedConnection);
}

/* The item widget is drawn over an empty list item, which is all a screen
 * reader sees. Give the item the filter's name and state as its text. */
void VisibilityItemWidget::SetListItem(QListWidget *list_, QListWidgetItem *item)
{
	list = list_;
	index = QPersistentModelIndex(list->indexFromItem(item));
	UpdateAccessibleText();
}

void VisibilityItemWidget::UpdateAccessibleText()
{
	QString name = QT_UTF8(obs_source_get_name(source));
	const char *stateKey = vis->isChecked() ? "Accessibility.Filters.ItemEnabled"
						 : "Accessibility.Filters.ItemDisabled";

	vis->setAccessibleName(QTStr("Accessibility.Filters.EnableFilter").arg(name));

	if (list && index.isValid()) {
		list->model()->setData(index, QTStr(stateKey).arg(name), Qt::AccessibleTextRole);
	}
}

void VisibilityItemWidget::OBSSourceEnabled(void *param, calldata_t *data)
{
	VisibilityItemWidget *window = static_cast<VisibilityItemWidget *>(param);
	bool enabled = calldata_bool(data, "enabled");

	QMetaObject::invokeMethod(window, &VisibilityItemWidget::SourceEnabled, enabled);
}

void VisibilityItemWidget::SourceEnabled(bool enabled)
{
	if (vis->isChecked() != enabled) {
		vis->setChecked(enabled);
	}
}

void VisibilityItemWidget::SetColor(const QColor &color, bool active_, bool selected_)
{
	/* Do not update unless the state has actually changed */
	if (active_ == active && selected_ == selected) {
		return;
	}

	QPalette pal = vis->palette();
	pal.setColor(QPalette::WindowText, color);
	vis->setPalette(pal);

	label->setStyleSheet(QString("color: %1;").arg(color.name()));

	active = active_;
	selected = selected_;
}

void SetupVisibilityItem(QListWidget *list, QListWidgetItem *item, obs_source_t *source)
{
	VisibilityItemWidget *baseWidget = new VisibilityItemWidget(source);

	list->setItemWidget(item, baseWidget);
	baseWidget->SetListItem(list, item);
}
