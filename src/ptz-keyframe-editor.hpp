/* SPDX-License-Identifier: GPLv2 */
#pragma once

#include "ptz-sequence.hpp"
#include <QDialog>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPolygonF>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <algorithm>

/* A local preview: scrubbing never sends commands to a camera. */
class PTZKeyTimeline : public QWidget {
public:
	PTZSeqStep clip;
	int cursor = 0, selected = -1;
	double pixelsPerSecond = 80;
	std::function<void()> selectionChanged;
	explicit PTZKeyTimeline(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumSize(600, 170);
		setFocusPolicy(Qt::StrongFocus);
	}
	int extent() const { return std::max(5000, clip.keyframeDuration() + 1000); }
	double scale() const { return std::min(pixelsPerSecond / 1000.0, double(std::max(1, width()-100))/extent()); }
	int x(int ms) const { return 86 + qRound(ms * scale()); }
	int timeAt(double px) const { return std::clamp(qRound((px-86)/scale()/100.0)*100, 0, 36000000); }
	void notify() { update(); if (selectionChanged) selectionChanged(); }
protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.fillRect(rect(), QColor(32, 34, 39));
		int tick = std::max(1000, int(70 / scale() / 1000) * 1000);
		p.setPen(QColor(110,115,125));
		for (int t=0; t<=extent(); t+=tick) {
			p.drawLine(x(t), 28, x(t), height());
			p.drawText(x(t)+4, 18, QString::number(t/1000.0) + " s");
		}
		QStringList names = {"Pan", "Tilt", "Zoom"};
		QColor colors[] = {QColor(231,106,104), QColor(126,202,137), QColor(111,167,244)};
		for (int lane=0; lane<3; ++lane) {
			int y=55+lane*42;
			p.setPen(colors[lane]); p.drawText(8,y+4,names[lane]);
			p.setPen(QColor(65,69,77)); p.drawLine(86,y,width(),y);
			for (int i=0; i<clip.keyframes.size(); ++i) {
				int px=x(clip.keyframes[i].timeMs);
				p.setPen(Qt::NoPen); p.setBrush(i==selected ? QColor(255,199,91) : colors[lane]);
				p.drawPolygon(QPolygonF{QPointF(px,y-7),QPointF(px+7,y),QPointF(px,y+7),QPointF(px-7,y)});
			}
		}
		p.setPen(QPen(QColor(95,185,255),2)); p.drawLine(x(cursor),26,x(cursor),height());
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button()!=Qt::LeftButton) return;
		setFocus(); dragging=false; selected=-1;
		if (e->position().y()>30) for (int i=0;i<clip.keyframes.size();++i)
			if (std::abs(x(clip.keyframes[i].timeMs)-e->position().x())<10) { selected=i; dragging=true; break; }
		cursor=selected>=0 ? clip.keyframes[selected].timeMs : timeAt(e->position().x());
		notify();
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!(e->buttons() & Qt::LeftButton)) return;
		int t=timeAt(e->position().x());
		if (dragging && selected>=0) {
			// Keep chronological order and prohibit duplicate timestamps.
			int lo=selected>0 ? clip.keyframes[selected-1].timeMs+100 : 0;
			int hi=selected+1<clip.keyframes.size() ? clip.keyframes[selected+1].timeMs-100 : 36000000;
			if (lo<=hi) clip.keyframes[selected].timeMs=std::clamp(t,lo,hi);
			cursor=clip.keyframes[selected].timeMs;
		} else cursor=t;
		notify();
	}
	void mouseReleaseEvent(QMouseEvent *) override { dragging=false; }
	void keyPressEvent(QKeyEvent *e) override
	{
		if (e->key()==Qt::Key_Delete && selected>=0) {
			clip.keyframes.removeAt(selected); selected=-1; notify();
		} else QWidget::keyPressEvent(e);
	}
private:
	bool dragging=false;
};

class PTZKeyframeDialog : public QDialog {
public:
	PTZKeyframeDialog(const PTZSeqStep &step, QWidget *parent,
			const QStringList &devices, const QList<uint32_t> &ids) : QDialog(parent), deviceIds(ids)
	{
		setWindowTitle(tr("PTZ Keyframe Editor")); resize(850,420);
		auto *root=new QVBoxLayout(this);
		auto *bar=new QHBoxLayout();
		device=new QComboBox(); device->addItems(devices);
		int index=ids.indexOf(step.deviceId);
		if (index<0) index=devices.indexOf(step.deviceName);
		if (index<0 && !step.deviceName.isEmpty()) { device->addItem(step.deviceName); deviceIds.append(step.deviceId); index=device->count()-1; }
		device->setCurrentIndex(std::max(0,index));
		bar->addWidget(new QLabel(tr("Camera"))); bar->addWidget(device,1);
		auto *play=new QPushButton(tr("Play / Pause preview")); bar->addWidget(play); root->addLayout(bar);
		timeline=new PTZKeyTimeline(); timeline->clip=step; timeline->clip.type=PTZStepType::Keyframes;
		root->addWidget(timeline,1);
		auto *edit=new QHBoxLayout();
		time=new QDoubleSpinBox(); time->setRange(0,36000); time->setDecimals(1); time->setSingleStep(.1); time->setSuffix(" s");
		edit->addWidget(time);
		pan=spin(-1,1,"Pan "); tilt=spin(-1,1,"Tilt "); zoom=spin(0,1,"Zoom ");
		for (auto *s : {pan,tilt,zoom}) edit->addWidget(s);
		interpolation=new QComboBox(); interpolation->addItems({tr("Linear"),tr("Smooth"),tr("Constant")});
		interpolation->setCurrentIndex(1); edit->addWidget(interpolation); root->addLayout(edit);
		auto *actions=new QHBoxLayout();
		auto *insert=new QPushButton(tr("Insert / Update key (I)"));
		auto *remove=new QPushButton(tr("Delete key"));
		insert->setShortcut(QKeySequence(Qt::Key_I));
		actions->addWidget(insert); actions->addWidget(remove); actions->addStretch(); root->addLayout(actions);
		auto *hint=new QLabel(tr("Drag diamonds to move keys (0.1 s grid). Click the ruler to scrub. Values are absolute positions.\nPreview is local; use Run in the sequence editor to move the camera. Interpolation applies to the next key."));
		hint->setWordWrap(true); root->addWidget(hint);
		auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); root->addWidget(buttons);
		connect(buttons,&QDialogButtonBox::accepted,this,[this](){ if (!timeline->clip.keyframes.isEmpty() && device->currentIndex()>=0) accept(); });
		connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
		buttons->button(QDialogButtonBox::Ok)->setEnabled(!devices.isEmpty() || !step.deviceName.isEmpty());
		timeline->selectionChanged=[this,buttons](){
			loadValues();
			buttons->button(QDialogButtonBox::Ok)->setEnabled(device->currentIndex()>=0 && !timeline->clip.keyframes.isEmpty());
		};
		connect(time,&QDoubleSpinBox::valueChanged,this,[this](double t){ if (!loading) { timeline->cursor=qRound(t*1000); timeline->selected=-1; timeline->notify(); } });
		connect(insert,&QPushButton::clicked,this,[this](){
			PTZKeyframe k{qRound(time->value()*1000),pan->value(),tilt->value(),zoom->value(),PTZKeyInterpolation(interpolation->currentIndex())};
			auto &keys=timeline->clip.keyframes;
			int i=0; while (i<keys.size() && keys[i].timeMs<k.timeMs) ++i;
			if (i<keys.size() && keys[i].timeMs==k.timeMs) keys[i]=k; else keys.insert(i,k);
			timeline->selected=i; timeline->notify();
		});
		connect(remove,&QPushButton::clicked,this,[this](){
			if (timeline->selected>=0) { timeline->clip.keyframes.removeAt(timeline->selected); timeline->selected=-1; timeline->notify(); }
		});
		clock=new QTimer(this); clock->setInterval(40);
		connect(play,&QPushButton::clicked,this,[this](){
			if (clock->isActive()) clock->stop(); else { if(timeline->cursor>=timeline->clip.keyframeDuration()) timeline->cursor=0; startTime=timeline->cursor; elapsed.restart(); clock->start(); }
		});
		connect(clock,&QTimer::timeout,this,[this](){
			timeline->cursor=std::min(startTime+int(elapsed.elapsed()),timeline->clip.keyframeDuration());
			timeline->selected=-1; timeline->notify();
			if (timeline->cursor>=timeline->clip.keyframeDuration()) clock->stop();
		});
		loadValues();
	}
	PTZSeqStep result() const
	{
		auto s=timeline->clip;
		int i=device->currentIndex();
		s.deviceId=i>=0 && i<deviceIds.size() ? deviceIds[i] : 0;
		s.deviceName=device->currentText(); return s;
	}
private:
	PTZKeyTimeline *timeline;
	QComboBox *device,*interpolation;
	QDoubleSpinBox *time,*pan,*tilt,*zoom;
	QList<uint32_t> deviceIds;
	QTimer *clock; QElapsedTimer elapsed; int startTime=0; bool loading=false;
	static QDoubleSpinBox *spin(double lo,double hi,const QString &prefix)
	{
		auto *s=new QDoubleSpinBox(); s->setRange(lo,hi); s->setDecimals(3); s->setSingleStep(.01); s->setPrefix(prefix); return s;
	}
	void loadValues()
	{
		loading=true;
		time->setValue(timeline->cursor/1000.0);
		auto k=timeline->clip.sampleKeyframes(timeline->cursor);
		pan->setValue(k.pan); tilt->setValue(k.tilt); zoom->setValue(k.zoom);
		interpolation->setCurrentIndex(int(k.interpolation));
		loading=false;
	}
};
