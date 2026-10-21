/* Pan Tilt Zoom Controls - Sequence editor window
 *
 * SPDX-License-Identifier: GPLv2
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <qt-wrappers.hpp>
#include <QMainWindow>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTimeEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QSplitter>
#include <QDialogButtonBox>
#include <QLocale>
#include <QMessageBox>
#include <algorithm>

#include "ptz.h"
#include "ptz-device.hpp"
#include "ptz-list-model.hpp"
#include "ptz-sequence.hpp"
#include "ptz-sequence-editor.hpp"

static QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QStringList sceneNames()
{
	QStringList list;
	char **names = obs_frontend_get_scene_names();
	for (char **n = names; n && *n; n++)
		list << QString::fromUtf8(*n);
	bfree(names);
	return list;
}

static QDoubleSpinBox *makeSpin(double min, double max, double step, int decimals = 3, const QString &suffix = QString())
{
	auto s = new QDoubleSpinBox();
	s->setRange(min, max);
	s->setSingleStep(step);
	s->setDecimals(decimals);
	if (!suffix.isEmpty())
		s->setSuffix(suffix);
	return s;
}

static QString seconds(int ms)
{
	return QString::number(ms / 1000.0, 'f', ms % 100 ? 2 : 1);
}

static QString sequenceName(const QString &id)
{
	auto eng = PTZSequenceEngine::instance();
	auto s = eng ? eng->find(id) : nullptr;
	return s ? s->name : QString("?");
}

/* ---------------------------------------------------------------------- */
/* Step dialog                                                            */
/* ---------------------------------------------------------------------- */

class PTZStepDialog : public QDialog {
public:
	PTZStepDialog(const PTZSeqStep &step, const QString &selfId, QWidget *parent) : QDialog(parent), orig(step)
	{
		setWindowTitle(T("PTZ.Seq.StepDialog.Title"));
		auto *form = new QFormLayout();
		this->form = form;

		typeCombo = new QComboBox();
		for (auto t : {PTZStepType::MoveAbs, PTZStepType::ZoomAbs, PTZStepType::MoveRamp,
			       PTZStepType::PresetRecall, PTZStepType::Home, PTZStepType::Stop, PTZStepType::Wait,
			       PTZStepType::SwitchScene, PTZStepType::StartSequence, PTZStepType::StopSequence})
			typeCombo->addItem(ptzStepTypeName(t), (int)t);
		typeCombo->setCurrentIndex(typeCombo->findData((int)step.type));
		form->addRow(T("PTZ.Seq.Field.Action"), typeCombo);

		/* Device */
		deviceCombo = new QComboBox();
		bool found = false;
		for (auto &name : ptzDeviceList.getDeviceNames()) {
			auto d = ptzDeviceList.getDeviceByName(name);
			deviceCombo->addItem(name, d ? (int)d->getId() : 0);
		}
		int idx = step.deviceId ? deviceCombo->findData((int)step.deviceId) : -1;
		if (idx < 0 && !step.deviceName.isEmpty())
			idx = deviceCombo->findText(step.deviceName);
		if (idx >= 0)
			found = true;
		if (!found && !step.deviceName.isEmpty()) {
			deviceCombo->addItem(step.deviceName + " " + T("PTZ.Seq.Missing"), (int)step.deviceId);
			idx = deviceCombo->count() - 1;
		}
		if (idx >= 0)
			deviceCombo->setCurrentIndex(idx);
		form->addRow(T("PTZ.Seq.Field.Device"), deviceCombo);

		/* Pan/Tilt (position or start speed) */
		panSp = makeSpin(-1, 1, 0.05);
		tiltSp = makeSpin(-1, 1, 0.05);
		panSp->setValue(step.pan);
		tiltSp->setValue(step.tilt);
		panSp->setPrefix(T("PTZ.Seq.Field.Pan") + " ");
		tiltSp->setPrefix(T("PTZ.Seq.Field.Tilt") + " ");
		auto *ptw = new QWidget();
		auto *ptl = new QHBoxLayout(ptw);
		ptl->setContentsMargins(0, 0, 0, 0);
		ptl->addWidget(panSp);
		ptl->addWidget(tiltSp);
		pantiltWidget = ptw;
		pantiltLabel = new QLabel();
		form->addRow(pantiltLabel, ptw);

		zoomSp = makeSpin(-1, 1, 0.05);
		zoomSp->setValue(step.zoom);
		zoomLabel = new QLabel();
		form->addRow(zoomLabel, zoomSp);

		/* Ramp end speeds */
		pan2Sp = makeSpin(-1, 1, 0.05);
		tilt2Sp = makeSpin(-1, 1, 0.05);
		zoom2Sp = makeSpin(-1, 1, 0.05);
		pan2Sp->setValue(step.pan2);
		tilt2Sp->setValue(step.tilt2);
		zoom2Sp->setValue(step.zoom2);
		pan2Sp->setPrefix(T("PTZ.Seq.Field.Pan") + " ");
		tilt2Sp->setPrefix(T("PTZ.Seq.Field.Tilt") + " ");
		zoom2Sp->setPrefix(T("PTZ.Seq.Field.Zoom") + " ");
		endWidget = new QWidget();
		auto *el = new QHBoxLayout(endWidget);
		el->setContentsMargins(0, 0, 0, 0);
		el->addWidget(pan2Sp);
		el->addWidget(tilt2Sp);
		el->addWidget(zoom2Sp);
		form->addRow(T("PTZ.Seq.Field.EndSpeed"), endWidget);

		easingCombo = new QComboBox();
		easingCombo->addItem(T("PTZ.Seq.Easing.Linear"), (int)PTZEasing::Linear);
		easingCombo->addItem(T("PTZ.Seq.Easing.EaseInOut"), (int)PTZEasing::EaseInOut);
		easingCombo->setCurrentIndex(easingCombo->findData((int)step.easing));
		form->addRow(T("PTZ.Seq.Field.Easing"), easingCombo);

		stopAtEndCheck = new QCheckBox(T("PTZ.Seq.Field.StopAtEnd"));
		stopAtEndCheck->setChecked(step.stopAtEnd);
		form->addRow(QString(), stopAtEndCheck);

		durationSp = makeSpin(0, 36000, 0.1, 2, " s");
		durationSp->setValue(step.durationMs / 1000.0);
		form->addRow(T("PTZ.Seq.Field.Duration"), durationSp);

		presetCombo = new QComboBox();
		form->addRow(T("PTZ.Seq.Field.Preset"), presetCombo);

		sceneCombo = new QComboBox();
		sceneCombo->addItems(sceneNames());
		int si = sceneCombo->findText(step.scene);
		if (si < 0 && !step.scene.isEmpty()) {
			sceneCombo->addItem(step.scene);
			si = sceneCombo->count() - 1;
		}
		if (si >= 0)
			sceneCombo->setCurrentIndex(si);
		form->addRow(T("PTZ.Seq.Field.Scene"), sceneCombo);

		seqCombo = new QComboBox();
		if (auto eng = PTZSequenceEngine::instance()) {
			for (auto &s : eng->sequences())
				if (s.id != selfId)
					seqCombo->addItem(s.name, s.id);
		}
		int qi = seqCombo->findData(step.sequenceId);
		if (qi >= 0)
			seqCombo->setCurrentIndex(qi);
		form->addRow(T("PTZ.Seq.Field.Sequence"), seqCombo);

		waitFinishCheck = new QCheckBox(T("PTZ.Seq.Field.WaitFinish"));
		waitFinishCheck->setChecked(step.waitFinish);
		form->addRow(QString(), waitFinishCheck);

		delaySp = makeSpin(0, 36000, 0.1, 2, " s");
		delaySp->setValue(step.delayMs / 1000.0);
		form->addRow(T("PTZ.Seq.Field.PauseAfter"), delaySp);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

		auto *lay = new QVBoxLayout(this);
		lay->addLayout(form);
		lay->addWidget(buttons);

		connect(typeCombo, &QComboBox::currentIndexChanged, this, [this]() { updateVisibility(); });
		connect(deviceCombo, &QComboBox::currentIndexChanged, this, [this]() { populatePresets(orig.preset); });
		populatePresets(step.preset);
		updateVisibility();
		setMinimumWidth(460);
	}

	PTZSeqStep result() const
	{
		PTZSeqStep s = orig;
		s.type = (PTZStepType)typeCombo->currentData().toInt();
		if (deviceCombo->count() > 0) {
			s.deviceId = (uint32_t)deviceCombo->currentData().toInt();
			auto d = s.deviceId ? ptzDeviceList.getDevice(s.deviceId) : nullptr;
			s.deviceName = d ? d->objectName() : deviceCombo->currentText();
		}
		s.pan = panSp->value();
		s.tilt = tiltSp->value();
		s.zoom = zoomSp->value();
		s.pan2 = pan2Sp->value();
		s.tilt2 = tilt2Sp->value();
		s.zoom2 = zoom2Sp->value();
		s.easing = (PTZEasing)easingCombo->currentData().toInt();
		s.stopAtEnd = stopAtEndCheck->isChecked();
		s.durationMs = (int)qRound(durationSp->value() * 1000.0);
		s.delayMs = (int)qRound(delaySp->value() * 1000.0);
		if (presetCombo->count() > 0)
			s.preset = presetCombo->currentData().toInt();
		s.scene = sceneCombo->currentText();
		s.sequenceId = seqCombo->currentData().toString();
		s.waitFinish = waitFinishCheck->isChecked();
		return s;
	}

private:
	PTZSeqStep orig;
	QFormLayout *form;
	QComboBox *typeCombo, *deviceCombo, *easingCombo, *presetCombo, *sceneCombo, *seqCombo;
	QDoubleSpinBox *panSp, *tiltSp, *zoomSp, *pan2Sp, *tilt2Sp, *zoom2Sp, *durationSp, *delaySp;
	QCheckBox *stopAtEndCheck, *waitFinishCheck;
	QWidget *pantiltWidget, *endWidget;
	QLabel *pantiltLabel, *zoomLabel;

	void populatePresets(int select)
	{
		presetCombo->clear();
		auto d = ptzDeviceList.getDevice((uint32_t)deviceCombo->currentData().toInt());
		if (d) {
			auto model = d->presetModel();
			for (int row = 0; model && row < model->rowCount(); row++) {
				auto index = model->index(row, 0);
				int id = model->data(index, Qt::UserRole).toInt();
				presetCombo->addItem(QString("%1 (%2)").arg(model->data(index, Qt::DisplayRole).toString()).arg(id),
						     id);
			}
		}
		int i = presetCombo->findData(select);
		if (i < 0) {
			presetCombo->addItem(QString("%1 %2").arg(T("PTZ.Seq.Field.Preset")).arg(select), select);
			i = presetCombo->count() - 1;
		}
		presetCombo->setCurrentIndex(i);
	}

	void setRow(QWidget *w, bool vis)
	{
		form->setRowVisible(w, vis);
	}

	void updateVisibility()
	{
		auto t = (PTZStepType)typeCombo->currentData().toInt();
		bool dev = t == PTZStepType::MoveAbs || t == PTZStepType::ZoomAbs || t == PTZStepType::MoveRamp ||
			   t == PTZStepType::PresetRecall || t == PTZStepType::Home || t == PTZStepType::Stop;
		setRow(deviceCombo, dev);
		setRow(pantiltWidget, t == PTZStepType::MoveAbs || t == PTZStepType::MoveRamp);
		setRow(zoomSp, t == PTZStepType::ZoomAbs || t == PTZStepType::MoveRamp);
		setRow(endWidget, t == PTZStepType::MoveRamp);
		setRow(easingCombo, t == PTZStepType::MoveRamp);
		setRow(stopAtEndCheck, t == PTZStepType::MoveRamp);
		setRow(durationSp, t == PTZStepType::MoveRamp || t == PTZStepType::Wait);
		setRow(presetCombo, t == PTZStepType::PresetRecall);
		setRow(sceneCombo, t == PTZStepType::SwitchScene);
		setRow(seqCombo, t == PTZStepType::StartSequence || t == PTZStepType::StopSequence);
		setRow(waitFinishCheck, t == PTZStepType::StartSequence);

		bool ramp = t == PTZStepType::MoveRamp;
		pantiltLabel->setText(ramp ? T("PTZ.Seq.Field.StartSpeed") : T("PTZ.Seq.Field.Position"));
		zoomLabel->setText(ramp ? T("PTZ.Seq.Field.StartZoomSpeed") : T("PTZ.Seq.Field.ZoomPosition"));
		/* Absolute zoom is 0..1, zoom speed is -1..1 */
		zoomSp->setMinimum(t == PTZStepType::ZoomAbs ? 0.0 : -1.0);
	}
};

/* ---------------------------------------------------------------------- */
/* Trigger dialog                                                         */
/* ---------------------------------------------------------------------- */

class PTZTriggerDialog : public QDialog {
public:
	PTZTriggerDialog(const PTZSeqTrigger &trig, const QString &selfId, QWidget *parent) : QDialog(parent), orig(trig)
	{
		setWindowTitle(T("PTZ.Seq.TriggerDialog.Title"));
		form = new QFormLayout();

		typeCombo = new QComboBox();
		for (auto t : {PTZTriggerType::SceneActive, PTZTriggerType::SceneInactive, PTZTriggerType::TimeOfDay,
			       PTZTriggerType::Interval, PTZTriggerType::SequenceFinished, PTZTriggerType::StreamStarted,
			       PTZTriggerType::StreamStopped, PTZTriggerType::RecordingStarted,
			       PTZTriggerType::RecordingStopped, PTZTriggerType::ObsStarted})
			typeCombo->addItem(ptzTriggerTypeName(t), (int)t);
		typeCombo->setCurrentIndex(typeCombo->findData((int)trig.type));
		form->addRow(T("PTZ.Seq.Field.Event"), typeCombo);

		sceneCombo = new QComboBox();
		sceneCombo->addItem(T("PTZ.Seq.AnyScene"), QString());
		for (auto &n : sceneNames())
			sceneCombo->addItem(n, n);
		int si = sceneCombo->findData(trig.scene);
		if (si < 0 && !trig.scene.isEmpty()) {
			sceneCombo->addItem(trig.scene, trig.scene);
			si = sceneCombo->count() - 1;
		}
		sceneCombo->setCurrentIndex(si < 0 ? 0 : si);
		form->addRow(T("PTZ.Seq.Field.Scene"), sceneCombo);

		stopOnLeaveCheck = new QCheckBox(T("PTZ.Seq.Field.StopOnLeave"));
		stopOnLeaveCheck->setChecked(trig.stopOnLeave);
		form->addRow(QString(), stopOnLeaveCheck);

		timeEdit = new QTimeEdit(trig.time);
		timeEdit->setDisplayFormat("HH:mm:ss");
		form->addRow(T("PTZ.Seq.Field.Time"), timeEdit);

		auto *wdw = new QWidget();
		auto *wdl = new QHBoxLayout(wdw);
		wdl->setContentsMargins(0, 0, 0, 0);
		for (int i = 0; i < 7; i++) {
			auto *cb = new QCheckBox(QLocale::system().dayName(i + 1, QLocale::ShortFormat));
			cb->setChecked(trig.weekdays & (1 << i));
			wdl->addWidget(cb);
			dayChecks.append(cb);
		}
		weekdayWidget = wdw;
		form->addRow(T("PTZ.Seq.Field.Weekdays"), wdw);

		intervalSp = new QSpinBox();
		intervalSp->setRange(1, 86400);
		intervalSp->setSuffix(" s");
		intervalSp->setValue(trig.intervalSec);
		form->addRow(T("PTZ.Seq.Field.Every"), intervalSp);

		seqCombo = new QComboBox();
		if (auto eng = PTZSequenceEngine::instance()) {
			for (auto &s : eng->sequences())
				if (s.id != selfId)
					seqCombo->addItem(s.name, s.id);
		}
		int qi = seqCombo->findData(trig.sequenceId);
		if (qi >= 0)
			seqCombo->setCurrentIndex(qi);
		form->addRow(T("PTZ.Seq.Field.Sequence"), seqCombo);

		delaySp = makeSpin(0, 36000, 0.1, 2, " s");
		delaySp->setValue(trig.delayMs / 1000.0);
		form->addRow(T("PTZ.Seq.Field.StartDelay"), delaySp);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

		auto *lay = new QVBoxLayout(this);
		lay->addLayout(form);
		lay->addWidget(buttons);

		connect(typeCombo, &QComboBox::currentIndexChanged, this, [this]() { updateVisibility(); });
		updateVisibility();
		setMinimumWidth(460);
	}

	PTZSeqTrigger result() const
	{
		PTZSeqTrigger t = orig;
		t.type = (PTZTriggerType)typeCombo->currentData().toInt();
		t.scene = sceneCombo->currentData().toString();
		t.stopOnLeave = stopOnLeaveCheck->isChecked();
		t.time = timeEdit->time();
		t.weekdays = 0;
		for (int i = 0; i < dayChecks.size(); i++)
			if (dayChecks[i]->isChecked())
				t.weekdays |= (1 << i);
		if (t.weekdays == 0)
			t.weekdays = 0x7F;
		t.intervalSec = intervalSp->value();
		t.sequenceId = seqCombo->currentData().toString();
		t.delayMs = (int)qRound(delaySp->value() * 1000.0);
		return t;
	}

private:
	PTZSeqTrigger orig;
	QFormLayout *form;
	QComboBox *typeCombo, *sceneCombo, *seqCombo;
	QCheckBox *stopOnLeaveCheck;
	QTimeEdit *timeEdit;
	QWidget *weekdayWidget;
	QList<QCheckBox *> dayChecks;
	QSpinBox *intervalSp;
	QDoubleSpinBox *delaySp;

	void updateVisibility()
	{
		auto t = (PTZTriggerType)typeCombo->currentData().toInt();
		bool scene = t == PTZTriggerType::SceneActive || t == PTZTriggerType::SceneInactive;
		form->setRowVisible(sceneCombo, scene);
		form->setRowVisible(stopOnLeaveCheck, t == PTZTriggerType::SceneActive);
		form->setRowVisible(timeEdit, t == PTZTriggerType::TimeOfDay);
		form->setRowVisible(weekdayWidget, t == PTZTriggerType::TimeOfDay);
		form->setRowVisible(intervalSp, t == PTZTriggerType::Interval);
		form->setRowVisible(seqCombo, t == PTZTriggerType::SequenceFinished);
	}
};

/* ---------------------------------------------------------------------- */
/* Editor window                                                          */
/* ---------------------------------------------------------------------- */

PTZSequenceEditor *PTZSequenceEditor::s_instance = nullptr;

void PTZSequenceEditor::showEditor()
{
	if (!PTZSequenceEngine::instance())
		return;
	if (!s_instance) {
		auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		s_instance = new PTZSequenceEditor(main_window);
		connect(s_instance, &QObject::destroyed, []() { s_instance = nullptr; });
	}
	s_instance->show();
	s_instance->raise();
	s_instance->activateWindow();
}

void PTZSequenceEditor::destroyEditor()
{
	if (s_instance) {
		auto *e = s_instance;
		s_instance = nullptr;
		delete e;
	}
}

PTZSequenceEditor::PTZSequenceEditor(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(T("PTZ.Seq.Window.Title"));
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	setAttribute(Qt::WA_DeleteOnClose, false);
	resize(1000, 620);

	/* Left: list of sequences */
	auto *left = new QWidget();
	auto *ll = new QVBoxLayout(left);
	ll->setContentsMargins(0, 0, 0, 0);
	ll->addWidget(new QLabel(T("PTZ.Seq.Sequences")));
	seqList = new QListWidget();
	ll->addWidget(seqList, 1);
	auto *lb = new QHBoxLayout();
	auto *addBtn = new QPushButton(T("PTZ.Seq.Add"));
	auto *dupBtn = new QPushButton(T("PTZ.Seq.Duplicate"));
	auto *delBtn = new QPushButton(T("PTZ.Seq.Remove"));
	lb->addWidget(addBtn);
	lb->addWidget(dupBtn);
	lb->addWidget(delBtn);
	ll->addLayout(lb);

	/* Right: detail pane */
	detailPane = new QWidget();
	auto *rl = new QVBoxLayout(detailPane);
	rl->setContentsMargins(0, 0, 0, 0);

	auto *form = new QFormLayout();
	nameEdit = new QLineEdit();
	enabledCheck = new QCheckBox(T("PTZ.Seq.Enabled"));
	auto *top = new QHBoxLayout();
	top->addWidget(nameEdit, 1);
	top->addWidget(enabledCheck);
	form->addRow(T("PTZ.Seq.Field.Name"), top);

	loopsSpin = new QSpinBox();
	loopsSpin->setRange(0, 9999);
	loopsSpin->setSpecialValueText(T("PTZ.Seq.Endless"));
	retriggerCombo = new QComboBox();
	retriggerCombo->addItem(T("PTZ.Seq.Retrigger.Ignore"));
	retriggerCombo->addItem(T("PTZ.Seq.Retrigger.Restart"));
	auto *opts = new QHBoxLayout();
	opts->addWidget(loopsSpin);
	opts->addSpacing(12);
	opts->addWidget(new QLabel(T("PTZ.Seq.Field.WhileRunning")));
	opts->addWidget(retriggerCombo);
	opts->addStretch(1);
	form->addRow(T("PTZ.Seq.Field.Repeat"), opts);
	rl->addLayout(form);

	auto *tabs = new QTabWidget();

	/* Steps tab */
	auto *stepsTab = new QWidget();
	auto *sl = new QVBoxLayout(stepsTab);
	stepTable = new QTableWidget(0, 6);
	stepTable->setHorizontalHeaderLabels({"#", T("PTZ.Seq.Col.Start"), T("PTZ.Seq.Col.Action"),
					      T("PTZ.Seq.Col.Details"), T("PTZ.Seq.Col.Duration"),
					      T("PTZ.Seq.Col.Pause")});
	stepTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	stepTable->setSelectionMode(QAbstractItemView::SingleSelection);
	stepTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	stepTable->verticalHeader()->hide();
	stepTable->horizontalHeader()->setStretchLastSection(false);
	stepTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
	sl->addWidget(stepTable, 1);
	auto *sb = new QHBoxLayout();
	auto *sAdd = new QPushButton(T("PTZ.Seq.Add"));
	auto *sEdit = new QPushButton(T("PTZ.Seq.Edit"));
	auto *sDup = new QPushButton(T("PTZ.Seq.Duplicate"));
	auto *sDel = new QPushButton(T("PTZ.Seq.Remove"));
	auto *sUp = new QPushButton(T("PTZ.Seq.Up"));
	auto *sDown = new QPushButton(T("PTZ.Seq.Down"));
	for (auto b : {sAdd, sEdit, sDup, sDel, sUp, sDown})
		sb->addWidget(b);
	sb->addStretch(1);
	sl->addLayout(sb);
	tabs->addTab(stepsTab, T("PTZ.Seq.Tab.Steps"));

	/* Triggers tab */
	auto *trigTab = new QWidget();
	auto *tl = new QVBoxLayout(trigTab);
	trigTable = new QTableWidget(0, 3);
	trigTable->setHorizontalHeaderLabels({T("PTZ.Seq.Col.Event"), T("PTZ.Seq.Col.Details"), T("PTZ.Seq.Col.Delay")});
	trigTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	trigTable->setSelectionMode(QAbstractItemView::SingleSelection);
	trigTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	trigTable->verticalHeader()->hide();
	trigTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	tl->addWidget(trigTable, 1);
	auto *tb = new QHBoxLayout();
	auto *tAdd = new QPushButton(T("PTZ.Seq.Add"));
	auto *tEdit = new QPushButton(T("PTZ.Seq.Edit"));
	auto *tDel = new QPushButton(T("PTZ.Seq.Remove"));
	for (auto b : {tAdd, tEdit, tDel})
		tb->addWidget(b);
	tb->addStretch(1);
	tl->addLayout(tb);
	auto *hint = new QLabel(T("PTZ.Seq.TriggerHint"));
	hint->setWordWrap(true);
	tl->addWidget(hint);
	tabs->addTab(trigTab, T("PTZ.Seq.Tab.Triggers"));
	rl->addWidget(tabs, 1);

	/* Run controls */
	auto *rb = new QHBoxLayout();
	runBtn = new QPushButton(T("PTZ.Seq.Run"));
	stopBtn = new QPushButton(T("PTZ.Seq.Stop"));
	auto *stopAllBtn = new QPushButton(T("PTZ.Seq.StopAll"));
	statusLabel = new QLabel();
	rb->addWidget(runBtn);
	rb->addWidget(stopBtn);
	rb->addWidget(stopAllBtn);
	rb->addWidget(statusLabel, 1);
	rl->addLayout(rb);

	auto *split = new QSplitter();
	split->addWidget(left);
	split->addWidget(detailPane);
	split->setStretchFactor(1, 1);
	split->setSizes({230, 770});
	auto *root = new QVBoxLayout(this);
	root->addWidget(split);

	auto *eng = PTZSequenceEngine::instance();

	connect(seqList, &QListWidget::currentRowChanged, this, [this]() { onSeqSelectionChanged(); });
	connect(addBtn, &QPushButton::clicked, this, [this, eng]() {
		QString id = eng->add(T("PTZ.Seq.NewName"));
		refreshList(id);
	});
	connect(dupBtn, &QPushButton::clicked, this, [this, eng]() {
		if (currentId.isEmpty())
			return;
		QString id = eng->duplicate(currentId);
		refreshList(id);
	});
	connect(delBtn, &QPushButton::clicked, this, [this, eng]() {
		if (currentId.isEmpty())
			return;
		if (QMessageBox::question(this, T("PTZ.Seq.Window.Title"), T("PTZ.Seq.ConfirmDelete")) !=
		    QMessageBox::Yes)
			return;
		eng->remove(currentId);
		currentId.clear();
		refreshList();
	});

	connect(nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
		if (auto s = cur()) {
			s->name = text;
			if (auto it = seqList->currentItem())
				it->setText(text);
			commit();
		}
	});
	connect(enabledCheck, &QCheckBox::toggled, this, [this](bool on) {
		if (loading)
			return;
		if (auto s = cur()) {
			s->enabled = on;
			commit();
			updateRunState();
		}
	});
	connect(loopsSpin, &QSpinBox::valueChanged, this, [this](int v) {
		if (loading)
			return;
		if (auto s = cur()) {
			s->loops = v;
			commit();
		}
	});
	connect(retriggerCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
		if (loading)
			return;
		if (auto s = cur()) {
			s->restartOnTrigger = (i == 1);
			commit();
		}
	});

	connect(sAdd, &QPushButton::clicked, this, [this]() { addStep(); });
	connect(sEdit, &QPushButton::clicked, this, [this]() { editStep(); });
	connect(sDup, &QPushButton::clicked, this, [this]() { duplicateStep(); });
	connect(sDel, &QPushButton::clicked, this, [this]() { removeStep(); });
	connect(sUp, &QPushButton::clicked, this, [this]() { moveStep(-1); });
	connect(sDown, &QPushButton::clicked, this, [this]() { moveStep(1); });
	connect(stepTable, &QTableWidget::cellDoubleClicked, this, [this]() { editStep(); });

	connect(tAdd, &QPushButton::clicked, this, [this]() { addTrigger(); });
	connect(tEdit, &QPushButton::clicked, this, [this]() { editTrigger(); });
	connect(tDel, &QPushButton::clicked, this, [this]() { removeTrigger(); });
	connect(trigTable, &QTableWidget::cellDoubleClicked, this, [this]() { editTrigger(); });

	connect(runBtn, &QPushButton::clicked, this, [this, eng]() {
		if (!currentId.isEmpty())
			eng->start(currentId);
	});
	connect(stopBtn, &QPushButton::clicked, this, [this, eng]() {
		if (!currentId.isEmpty())
			eng->stop(currentId);
	});
	connect(stopAllBtn, &QPushButton::clicked, this, [eng]() { eng->stopAll(); });

	connect(eng, &PTZSequenceEngine::runStateChanged, this, [this]() { updateRunState(); });

	refreshList();
}

PTZSequenceEditor::~PTZSequenceEditor() {}

PTZSequence *PTZSequenceEditor::cur()
{
	auto eng = PTZSequenceEngine::instance();
	return (eng && !currentId.isEmpty()) ? eng->find(currentId) : nullptr;
}

void PTZSequenceEditor::commit()
{
	if (auto eng = PTZSequenceEngine::instance())
		eng->changed();
}

void PTZSequenceEditor::refreshList(const QString &select)
{
	auto eng = PTZSequenceEngine::instance();
	if (!eng)
		return;
	QString want = select.isEmpty() ? currentId : select;
	loading = true;
	seqList->clear();
	int row = -1;
	for (auto &s : eng->sequences()) {
		auto *item = new QListWidgetItem(s.name);
		item->setData(Qt::UserRole, s.id);
		seqList->addItem(item);
		if (s.id == want)
			row = seqList->count() - 1;
	}
	if (row < 0 && seqList->count() > 0)
		row = 0;
	loading = false;
	if (row >= 0)
		seqList->setCurrentRow(row);
	else {
		currentId.clear();
		loadCurrent();
	}
	updateRunState();
}

void PTZSequenceEditor::onSeqSelectionChanged()
{
	if (loading)
		return;
	auto *item = seqList->currentItem();
	currentId = item ? item->data(Qt::UserRole).toString() : QString();
	loadCurrent();
	updateRunState();
}

void PTZSequenceEditor::loadCurrent()
{
	auto s = cur();
	detailPane->setEnabled(s != nullptr);
	loading = true;
	nameEdit->setText(s ? s->name : QString());
	enabledCheck->setChecked(s ? s->enabled : false);
	loopsSpin->setValue(s ? s->loops : 1);
	retriggerCombo->setCurrentIndex(s && s->restartOnTrigger ? 1 : 0);
	loading = false;
	refreshSteps();
	refreshTriggers();
}

static QString stepDetails(const PTZSeqStep &s)
{
	auto f = [](double v) { return QString::number(v, 'f', 2); };
	QString dev = s.deviceName;
	switch (s.type) {
	case PTZStepType::MoveAbs:
		return QString("%1: %2 %3, %4 %5").arg(dev, T("PTZ.Seq.Field.Pan"), f(s.pan), T("PTZ.Seq.Field.Tilt"), f(s.tilt));
	case PTZStepType::ZoomAbs:
		return QString("%1: %2 %3").arg(dev, T("PTZ.Seq.Field.Zoom"), f(s.zoom));
	case PTZStepType::MoveRamp:
		return QString("%1: (%2 / %3 / %4) → (%5 / %6 / %7)")
			.arg(dev, f(s.pan), f(s.tilt), f(s.zoom), f(s.pan2), f(s.tilt2), f(s.zoom2));
	case PTZStepType::PresetRecall:
		return QString("%1: %2 %3").arg(dev, T("PTZ.Seq.Field.Preset")).arg(s.preset);
	case PTZStepType::Home:
	case PTZStepType::Stop:
		return dev;
	case PTZStepType::Wait:
		return QString();
	case PTZStepType::SwitchScene:
		return s.scene;
	case PTZStepType::StartSequence:
		return sequenceName(s.sequenceId) + (s.waitFinish ? " (" + T("PTZ.Seq.Field.WaitFinishShort") + ")" : QString());
	case PTZStepType::StopSequence:
		return sequenceName(s.sequenceId);
	}
	return QString();
}

void PTZSequenceEditor::refreshSteps()
{
	int keep = stepTable->currentRow();
	stepTable->setRowCount(0);
	auto s = cur();
	if (!s)
		return;
	int t = 0;
	for (int i = 0; i < s->steps.size(); i++) {
		auto &st = s->steps[i];
		stepTable->insertRow(i);
		bool timed = st.type == PTZStepType::Wait || st.type == PTZStepType::MoveRamp;
		QStringList cells = {QString::number(i + 1), seconds(t), ptzStepTypeName(st.type), stepDetails(st),
				     timed ? seconds(st.durationMs) : QString("-"), seconds(st.delayMs)};
		for (int c = 0; c < cells.size(); c++)
			stepTable->setItem(i, c, new QTableWidgetItem(cells[c]));
		t += st.totalMs();
	}
	stepTable->resizeColumnToContents(0);
	stepTable->resizeColumnToContents(1);
	stepTable->resizeColumnToContents(2);
	stepTable->resizeColumnToContents(4);
	stepTable->resizeColumnToContents(5);
	if (keep >= 0 && keep < stepTable->rowCount())
		stepTable->selectRow(keep);
}

static QString triggerDetails(const PTZSeqTrigger &t)
{
	switch (t.type) {
	case PTZTriggerType::SceneActive:
	case PTZTriggerType::SceneInactive: {
		QString d = t.scene.isEmpty() ? T("PTZ.Seq.AnyScene") : t.scene;
		if (t.type == PTZTriggerType::SceneActive && t.stopOnLeave)
			d += " – " + T("PTZ.Seq.Field.StopOnLeave");
		return d;
	}
	case PTZTriggerType::TimeOfDay: {
		QStringList days;
		for (int i = 0; i < 7; i++)
			if (t.weekdays & (1 << i))
				days << QLocale::system().dayName(i + 1, QLocale::ShortFormat);
		return t.time.toString("HH:mm:ss") + (t.weekdays == 0x7F ? QString() : ", " + days.join(" "));
	}
	case PTZTriggerType::Interval:
		return QString("%1 %2 s").arg(T("PTZ.Seq.Field.Every")).arg(t.intervalSec);
	case PTZTriggerType::SequenceFinished:
		return sequenceName(t.sequenceId);
	default:
		return QString();
	}
}

void PTZSequenceEditor::refreshTriggers()
{
	int keep = trigTable->currentRow();
	trigTable->setRowCount(0);
	auto s = cur();
	if (!s)
		return;
	for (int i = 0; i < s->triggers.size(); i++) {
		auto &t = s->triggers[i];
		trigTable->insertRow(i);
		trigTable->setItem(i, 0, new QTableWidgetItem(ptzTriggerTypeName(t.type)));
		trigTable->setItem(i, 1, new QTableWidgetItem(triggerDetails(t)));
		trigTable->setItem(i, 2, new QTableWidgetItem(seconds(t.delayMs)));
	}
	trigTable->resizeColumnToContents(0);
	trigTable->resizeColumnToContents(2);
	if (keep >= 0 && keep < trigTable->rowCount())
		trigTable->selectRow(keep);
}

void PTZSequenceEditor::updateRunState()
{
	auto eng = PTZSequenceEngine::instance();
	if (!eng)
		return;
	for (int i = 0; i < seqList->count(); i++) {
		auto *item = seqList->item(i);
		QString id = item->data(Qt::UserRole).toString();
		auto s = eng->find(id);
		if (!s)
			continue;
		QString text = (eng->isRunning(id) ? QString("▶ ") : QString()) + s->name;
		if (!s->enabled)
			text += " (" + T("PTZ.Seq.Disabled") + ")";
		if (item->text() != text)
			item->setText(text);
	}
	auto s = cur();
	bool running = s && eng->isRunning(currentId);
	runBtn->setEnabled(s != nullptr);
	stopBtn->setEnabled(running);
	if (!s) {
		statusLabel->clear();
		return;
	}
	if (running) {
		int step = eng->runningStep(currentId);
		statusLabel->setText(QString("%1 – %2 %3/%4")
					     .arg(T("PTZ.Seq.Running"), T("PTZ.Seq.Step"))
					     .arg(std::max(step + 1, 1))
					     .arg(s->steps.size()));
	} else {
		statusLabel->setText(QString("%1 – %2 s").arg(T("PTZ.Seq.Idle"), seconds(s->totalMs())));
	}
}

/* ---- step actions ---- */

void PTZSequenceEditor::addStep()
{
	auto s = cur();
	if (!s)
		return;
	PTZSeqStep st;
	st.type = PTZStepType::MoveRamp;
	if (!ptzDeviceList.getDeviceNames().isEmpty()) {
		auto d = ptzDeviceList.getDeviceByName(ptzDeviceList.getDeviceNames().first());
		if (d) {
			st.deviceId = d->getId();
			st.deviceName = d->objectName();
		}
	}
	/* Default to the device of the previous step */
	for (int i = s->steps.size() - 1; i >= 0; i--) {
		if (!s->steps[i].deviceName.isEmpty()) {
			st.deviceId = s->steps[i].deviceId;
			st.deviceName = s->steps[i].deviceName;
			break;
		}
	}
	PTZStepDialog dlg(st, s->id, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	int row = stepTable->currentRow();
	int at = (row >= 0 && row < s->steps.size()) ? row + 1 : s->steps.size();
	s->steps.insert(at, dlg.result());
	commit();
	refreshSteps();
	stepTable->selectRow(at);
	updateRunState();
}

void PTZSequenceEditor::editStep()
{
	auto s = cur();
	int row = stepTable->currentRow();
	if (!s || row < 0 || row >= s->steps.size())
		return;
	PTZStepDialog dlg(s->steps[row], s->id, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	s->steps[row] = dlg.result();
	commit();
	refreshSteps();
	updateRunState();
}

void PTZSequenceEditor::duplicateStep()
{
	auto s = cur();
	int row = stepTable->currentRow();
	if (!s || row < 0 || row >= s->steps.size())
		return;
	s->steps.insert(row + 1, s->steps[row]);
	commit();
	refreshSteps();
	stepTable->selectRow(row + 1);
	updateRunState();
}

void PTZSequenceEditor::removeStep()
{
	auto s = cur();
	int row = stepTable->currentRow();
	if (!s || row < 0 || row >= s->steps.size())
		return;
	s->steps.removeAt(row);
	commit();
	refreshSteps();
	updateRunState();
}

void PTZSequenceEditor::moveStep(int delta)
{
	auto s = cur();
	int row = stepTable->currentRow();
	int to = row + delta;
	if (!s || row < 0 || to < 0 || to >= s->steps.size())
		return;
	s->steps.swapItemsAt(row, to);
	commit();
	refreshSteps();
	stepTable->selectRow(to);
}

/* ---- trigger actions ---- */

void PTZSequenceEditor::addTrigger()
{
	auto s = cur();
	if (!s)
		return;
	PTZTriggerDialog dlg(PTZSeqTrigger(), s->id, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	s->triggers.append(dlg.result());
	commit();
	refreshTriggers();
	trigTable->selectRow(s->triggers.size() - 1);
}

void PTZSequenceEditor::editTrigger()
{
	auto s = cur();
	int row = trigTable->currentRow();
	if (!s || row < 0 || row >= s->triggers.size())
		return;
	PTZTriggerDialog dlg(s->triggers[row], s->id, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	s->triggers[row] = dlg.result();
	commit();
	refreshTriggers();
}

void PTZSequenceEditor::removeTrigger()
{
	auto s = cur();
	int row = trigTable->currentRow();
	if (!s || row < 0 || row >= s->triggers.size())
		return;
	s->triggers.removeAt(row);
	commit();
	refreshTriggers();
}

extern "C" void ptz_sequence_editor_show(void)
{
	PTZSequenceEditor::showEditor();
}

extern "C" void ptz_sequence_editor_destroy(void)
{
	PTZSequenceEditor::destroyEditor();
}

extern "C" void ptz_load_sequence_menu(void)
{
	auto *action = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("PTZ.Seq.MenuTitle"));
	action->connect(action, &QAction::triggered, []() { ptz_sequence_editor_show(); });
}
