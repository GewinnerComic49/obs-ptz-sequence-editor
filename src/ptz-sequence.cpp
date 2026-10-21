/* Pan Tilt Zoom Controls - Sequence engine
 *
 * SPDX-License-Identifier: GPLv2
 */

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include <QtMath>
#include <algorithm>

#include "ptz.h"
#include "ptz-device.hpp"
#include "ptz-list-model.hpp"
#include "ptz-sequence.hpp"

PTZSequenceEngine *PTZSequenceEngine::s_instance = nullptr;

/* ---------------------------------------------------------------------- */
/* Names / (de)serialisation                                              */
/* ---------------------------------------------------------------------- */

static const struct {
	PTZStepType type;
	const char *key;
} stepKeys[] = {
	{PTZStepType::MoveAbs, "move_abs"},
	{PTZStepType::ZoomAbs, "zoom_abs"},
	{PTZStepType::MoveRamp, "move_ramp"},
	{PTZStepType::PresetRecall, "preset_recall"},
	{PTZStepType::Home, "home"},
	{PTZStepType::Stop, "stop"},
	{PTZStepType::Wait, "wait"},
	{PTZStepType::SwitchScene, "switch_scene"},
	{PTZStepType::StartSequence, "start_sequence"},
	{PTZStepType::StopSequence, "stop_sequence"},
};

static const struct {
	PTZTriggerType type;
	const char *key;
} triggerKeys[] = {
	{PTZTriggerType::SceneActive, "scene_active"},
	{PTZTriggerType::SceneInactive, "scene_inactive"},
	{PTZTriggerType::TimeOfDay, "time_of_day"},
	{PTZTriggerType::Interval, "interval"},
	{PTZTriggerType::SequenceFinished, "sequence_finished"},
	{PTZTriggerType::StreamStarted, "stream_started"},
	{PTZTriggerType::StreamStopped, "stream_stopped"},
	{PTZTriggerType::RecordingStarted, "recording_started"},
	{PTZTriggerType::RecordingStopped, "recording_stopped"},
	{PTZTriggerType::ObsStarted, "obs_started"},
};

QString ptzStepTypeKey(PTZStepType t)
{
	for (auto &e : stepKeys)
		if (e.type == t)
			return e.key;
	return "wait";
}

QString ptzTriggerTypeKey(PTZTriggerType t)
{
	for (auto &e : triggerKeys)
		if (e.type == t)
			return e.key;
	return "scene_active";
}

QString ptzStepTypeName(PTZStepType t)
{
	const QByteArray key = ("PTZ.Seq.Step." + ptzStepTypeKey(t)).toUtf8();
	return QString::fromUtf8(obs_module_text(key.constData()));
}

QString ptzTriggerTypeName(PTZTriggerType t)
{
	const QByteArray key = ("PTZ.Seq.Trigger." + ptzTriggerTypeKey(t)).toUtf8();
	return QString::fromUtf8(obs_module_text(key.constData()));
}

static PTZStepType stepTypeFromKey(const QString &k)
{
	for (auto &e : stepKeys)
		if (k == e.key)
			return e.type;
	return PTZStepType::Wait;
}

static PTZTriggerType triggerTypeFromKey(const QString &k)
{
	for (auto &e : triggerKeys)
		if (k == e.key)
			return e.type;
	return PTZTriggerType::SceneActive;
}

int PTZSeqStep::totalMs() const
{
	int t = std::max(0, delayMs);
	if (type == PTZStepType::Wait || type == PTZStepType::MoveRamp)
		t += std::max(0, durationMs);
	return t;
}

QJsonObject PTZSeqStep::toJson() const
{
	QJsonObject o;
	o["type"] = ptzStepTypeKey(type);
	o["device_id"] = (int)deviceId;
	o["device_name"] = deviceName;
	o["pan"] = pan;
	o["tilt"] = tilt;
	o["zoom"] = zoom;
	o["pan2"] = pan2;
	o["tilt2"] = tilt2;
	o["zoom2"] = zoom2;
	o["easing"] = easing == PTZEasing::EaseInOut ? "ease_in_out" : "linear";
	o["stop_at_end"] = stopAtEnd;
	o["duration_ms"] = durationMs;
	o["delay_ms"] = delayMs;
	o["preset"] = preset;
	o["scene"] = scene;
	o["sequence_id"] = sequenceId;
	o["wait_finish"] = waitFinish;
	return o;
}

PTZSeqStep PTZSeqStep::fromJson(const QJsonObject &o)
{
	PTZSeqStep s;
	s.type = stepTypeFromKey(o["type"].toString());
	s.deviceId = (uint32_t)o["device_id"].toInt();
	s.deviceName = o["device_name"].toString();
	s.pan = o["pan"].toDouble();
	s.tilt = o["tilt"].toDouble();
	s.zoom = o["zoom"].toDouble();
	s.pan2 = o["pan2"].toDouble();
	s.tilt2 = o["tilt2"].toDouble();
	s.zoom2 = o["zoom2"].toDouble();
	s.easing = o["easing"].toString() == "ease_in_out" ? PTZEasing::EaseInOut : PTZEasing::Linear;
	s.stopAtEnd = o["stop_at_end"].toBool(true);
	s.durationMs = o["duration_ms"].toInt(1000);
	s.delayMs = o["delay_ms"].toInt(0);
	s.preset = o["preset"].toInt(1);
	s.scene = o["scene"].toString();
	s.sequenceId = o["sequence_id"].toString();
	s.waitFinish = o["wait_finish"].toBool(false);
	return s;
}

QJsonObject PTZSeqTrigger::toJson() const
{
	QJsonObject o;
	o["type"] = ptzTriggerTypeKey(type);
	o["scene"] = scene;
	o["time"] = time.toString("HH:mm:ss");
	o["weekdays"] = weekdays;
	o["interval_sec"] = intervalSec;
	o["sequence_id"] = sequenceId;
	o["delay_ms"] = delayMs;
	o["stop_on_leave"] = stopOnLeave;
	return o;
}

PTZSeqTrigger PTZSeqTrigger::fromJson(const QJsonObject &o)
{
	PTZSeqTrigger t;
	t.type = triggerTypeFromKey(o["type"].toString());
	t.scene = o["scene"].toString();
	t.time = QTime::fromString(o["time"].toString(), "HH:mm:ss");
	if (!t.time.isValid())
		t.time = QTime(12, 0, 0);
	t.weekdays = o["weekdays"].toInt(0x7F);
	t.intervalSec = std::max(1, o["interval_sec"].toInt(60));
	t.sequenceId = o["sequence_id"].toString();
	t.delayMs = o["delay_ms"].toInt(0);
	t.stopOnLeave = o["stop_on_leave"].toBool(false);
	return t;
}

int PTZSequence::totalMs() const
{
	int t = 0;
	for (auto &s : steps)
		t += s.totalMs();
	return t;
}

QJsonObject PTZSequence::toJson() const
{
	QJsonObject o;
	o["id"] = id;
	o["name"] = name;
	o["enabled"] = enabled;
	o["loops"] = loops;
	o["restart_on_trigger"] = restartOnTrigger;
	QJsonArray s;
	for (auto &st : steps)
		s.append(st.toJson());
	o["steps"] = s;
	QJsonArray t;
	for (auto &tr : triggers)
		t.append(tr.toJson());
	o["triggers"] = t;
	return o;
}

PTZSequence PTZSequence::fromJson(const QJsonObject &o)
{
	PTZSequence q;
	q.id = o["id"].toString();
	if (q.id.isEmpty())
		q.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	q.name = o["name"].toString();
	q.enabled = o["enabled"].toBool(true);
	q.loops = std::max(0, o["loops"].toInt(1));
	q.restartOnTrigger = o["restart_on_trigger"].toBool(false);
	for (auto v : o["steps"].toArray())
		q.steps.append(PTZSeqStep::fromJson(v.toObject()));
	for (auto v : o["triggers"].toArray())
		q.triggers.append(PTZSeqTrigger::fromJson(v.toObject()));
	return q;
}

/* ---------------------------------------------------------------------- */
/* Device helpers                                                         */
/* ---------------------------------------------------------------------- */

static PTZDevice *resolveDevice(const PTZSeqStep &s)
{
	PTZDevice *d = s.deviceId ? ptzDeviceList.getDevice(s.deviceId) : nullptr;
	if (!d && !s.deviceName.isEmpty())
		d = ptzDeviceList.getDeviceByName(s.deviceName);
	return d;
}

static bool callDeviceById(uint32_t id, const char *method, calldata_t *cd = nullptr)
{
	auto idx = ptzDeviceList.indexFromDeviceId(id);
	if (!idx.isValid())
		return false;
	return ptzDeviceList.callDevice(idx, method, cd);
}

static bool callStepDevice(const PTZSeqStep &s, const char *method, calldata_t *cd = nullptr)
{
	PTZDevice *d = resolveDevice(s);
	if (!d) {
		blog(LOG_WARNING, "sequence: device '%s' not found", QT_TO_UTF8(s.deviceName));
		return false;
	}
	return callDeviceById(d->getId(), method, cd);
}

static void sendMove(const PTZSeqStep &s, double pan, double tilt, double zoom)
{
	calldata cd = {};
	calldata_set_float(&cd, "pan", pan);
	calldata_set_float(&cd, "tilt", tilt);
	calldata_set_float(&cd, "zoom", zoom);
	callStepDevice(s, "ptz_move", &cd);
	calldata_free(&cd);
}

/* ---------------------------------------------------------------------- */
/* Engine lifecycle                                                       */
/* ---------------------------------------------------------------------- */

PTZSequenceEngine *PTZSequenceEngine::instance()
{
	return s_instance;
}

void PTZSequenceEngine::create()
{
	if (!s_instance)
		s_instance = new PTZSequenceEngine();
}

void PTZSequenceEngine::destroy()
{
	delete s_instance;
	s_instance = nullptr;
}

PTZSequenceEngine::PTZSequenceEngine() : QObject()
{
	m_mono.start();
	m_lastTick = QDateTime::currentDateTime();

	char *file = obs_module_config_path("sequences.json");
	if (file) {
		m_path = QString::fromUtf8(file);
		bfree(file);
	}

	m_saveTimer.setSingleShot(true);
	m_saveTimer.setInterval(500);
	connect(&m_saveTimer, &QTimer::timeout, this, [this]() { save(); });

	m_tickTimer.setInterval(500);
	connect(&m_tickTimer, &QTimer::timeout, this, [this]() { onTick(); });
	m_tickTimer.start();

	obs_frontend_add_event_callback(onFrontendEvent, this);
	load();
}

PTZSequenceEngine::~PTZSequenceEngine()
{
	obs_frontend_remove_event_callback(onFrontendEvent, this);
	m_tickTimer.stop();
	stopAll();
	if (m_loaded)
		save();
}

PTZSequence *PTZSequenceEngine::find(const QString &id)
{
	for (auto &s : m_sequences)
		if (s.id == id)
			return &s;
	return nullptr;
}

QString PTZSequenceEngine::add(const QString &name)
{
	PTZSequence s;
	s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	s.name = name;
	m_sequences.append(s);
	changed();
	emit listChanged();
	return s.id;
}

QString PTZSequenceEngine::duplicate(const QString &id)
{
	auto src = find(id);
	if (!src)
		return QString();
	PTZSequence s = *src;
	s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	s.name += " (copy)";
	m_sequences.append(s);
	changed();
	emit listChanged();
	return s.id;
}

void PTZSequenceEngine::remove(const QString &id)
{
	stop(id);
	for (int i = 0; i < m_sequences.size(); i++) {
		if (m_sequences[i].id == id) {
			m_sequences.removeAt(i);
			break;
		}
	}
	changed();
	emit listChanged();
}

void PTZSequenceEngine::changed()
{
	m_intervalDue.clear();
	m_saveTimer.start();
}

void PTZSequenceEngine::load()
{
	m_loaded = true;
	if (m_path.isEmpty())
		return;
	QFile f(m_path);
	if (!f.open(QIODevice::ReadOnly))
		return;
	QJsonParseError err;
	auto doc = QJsonDocument::fromJson(f.readAll(), &err);
	if (err.error != QJsonParseError::NoError) {
		blog(LOG_WARNING, "sequences.json could not be parsed: %s", QT_TO_UTF8(err.errorString()));
		return;
	}
	m_sequences.clear();
	for (auto v : doc.object()["sequences"].toArray())
		m_sequences.append(PTZSequence::fromJson(v.toObject()));
}

void PTZSequenceEngine::save()
{
	if (m_path.isEmpty())
		return;
	QJsonArray arr;
	for (auto &s : m_sequences)
		arr.append(s.toJson());
	QJsonObject root;
	root["version"] = 1;
	root["sequences"] = arr;

	QByteArray dir = m_path.left(m_path.lastIndexOf('/')).toUtf8();
	if (!dir.isEmpty())
		os_mkdirs(dir.constData());

	QSaveFile f(m_path);
	if (!f.open(QIODevice::WriteOnly)) {
		blog(LOG_WARNING, "could not write %s", QT_TO_UTF8(m_path));
		return;
	}
	f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	f.commit();
}

/* ---------------------------------------------------------------------- */
/* Triggers                                                               */
/* ---------------------------------------------------------------------- */

void PTZSequenceEngine::onFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	static_cast<PTZSequenceEngine *>(ptr)->handleFrontendEvent(event);
}

static QString currentSceneName()
{
	OBSSourceAutoRelease src = obs_frontend_get_current_scene();
	return src ? QString::fromUtf8(obs_source_get_name(src)) : QString();
}

void PTZSequenceEngine::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		m_lastScene = currentSceneName();
		fireTriggers(PTZTriggerType::ObsStarted);
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED: {
		QString now = currentSceneName();
		QString prev = m_lastScene;
		m_lastScene = now;
		if (now == prev)
			break;
		/* Stop sequences bound to the scene that was just left */
		for (auto &seq : m_sequences) {
			if (!seq.enabled)
				continue;
			for (auto &t : seq.triggers)
				if (t.type == PTZTriggerType::SceneActive && t.stopOnLeave && t.scene == prev &&
				    !prev.isEmpty())
					stop(seq.id);
		}
		if (!prev.isEmpty())
			fireTriggers(PTZTriggerType::SceneInactive, prev);
		fireTriggers(PTZTriggerType::SceneActive, now);
		break;
	}
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		fireTriggers(PTZTriggerType::StreamStarted);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		fireTriggers(PTZTriggerType::StreamStopped);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		fireTriggers(PTZTriggerType::RecordingStarted);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		fireTriggers(PTZTriggerType::RecordingStopped);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_EXIT:
		stopAll();
		if (event == OBS_FRONTEND_EVENT_EXIT && m_loaded)
			save();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		m_lastScene = currentSceneName();
		break;
	default:
		break;
	}
}

void PTZSequenceEngine::fireTriggers(PTZTriggerType type, const QString &scene, const QString &sequenceId)
{
	/* Copy ids first: starting sequences must not invalidate our iteration */
	struct Hit {
		QString id;
		int delay;
	};
	QList<Hit> hits;
	for (auto &seq : m_sequences) {
		if (!seq.enabled)
			continue;
		for (auto &t : seq.triggers) {
			if (t.type != type)
				continue;
			if ((type == PTZTriggerType::SceneActive || type == PTZTriggerType::SceneInactive) &&
			    !t.scene.isEmpty() && t.scene != scene)
				continue;
			if (type == PTZTriggerType::SequenceFinished && t.sequenceId != sequenceId)
				continue;
			hits.append({seq.id, t.delayMs});
		}
	}
	for (auto &h : hits)
		fire(h.id, h.delay);
}

void PTZSequenceEngine::fire(const QString &seqId, int delayMs)
{
	if (delayMs <= 0) {
		start(seqId);
		return;
	}
	quint64 gen = m_cancelGen.value(seqId);
	QTimer::singleShot(delayMs, this, [this, seqId, gen]() {
		if (m_cancelGen.value(seqId) == gen)
			start(seqId);
	});
}

void PTZSequenceEngine::onTick()
{
	QDateTime now = QDateTime::currentDateTime();
	QDateTime last = m_lastTick;
	m_lastTick = now;
	/* Clock jumped (sleep, manual change): do not replay anything */
	if (last > now || last.secsTo(now) > 5)
		last = now.addMSecs(-500);

	qint64 mono = m_mono.elapsed();

	struct Hit {
		QString id;
		int delay;
	};
	QList<Hit> hits;
	QSet<QString> liveKeys;

	for (auto &seq : m_sequences) {
		if (!seq.enabled)
			continue;
		for (int i = 0; i < seq.triggers.size(); i++) {
			auto &t = seq.triggers[i];
			if (t.type == PTZTriggerType::TimeOfDay) {
				QDateTime target(now.date(), t.time);
				int wd = target.date().dayOfWeek() - 1;
				if (!(t.weekdays & (1 << wd)))
					continue;
				if (last < target && target <= now)
					hits.append({seq.id, t.delayMs});
			} else if (t.type == PTZTriggerType::Interval) {
				QString key = seq.id + "#" + QString::number(i);
				liveKeys.insert(key);
				qint64 interval = std::max(1, t.intervalSec) * 1000LL;
				if (!m_intervalDue.contains(key)) {
					m_intervalDue[key] = mono + interval;
					continue;
				}
				if (mono >= m_intervalDue[key]) {
					m_intervalDue[key] = mono + interval;
					hits.append({seq.id, t.delayMs});
				}
			}
		}
	}
	for (auto it = m_intervalDue.begin(); it != m_intervalDue.end();) {
		if (!liveKeys.contains(it.key()))
			it = m_intervalDue.erase(it);
		else
			++it;
	}
	for (auto &h : hits)
		fire(h.id, h.delay);
}

/* ---------------------------------------------------------------------- */
/* Execution                                                              */
/* ---------------------------------------------------------------------- */

int PTZSequenceEngine::runningStep(const QString &id) const
{
	auto it = m_runners.constFind(id);
	return it == m_runners.constEnd() ? -1 : it->step;
}

void PTZSequenceEngine::start(const QString &id)
{
	auto seq = find(id);
	if (!seq || seq->steps.isEmpty())
		return;
	if (m_runners.contains(id)) {
		if (!seq->restartOnTrigger)
			return;
		stop(id);
	}

	Runner &r = m_runners[id];
	r.seq = *seq; /* snapshot: edits while running do not affect this run */
	r.step = -1;
	r.loop = 0;
	r.timer = new QTimer(this);
	r.timer->setSingleShot(true);
	connect(r.timer, &QTimer::timeout, this, [this, id]() { runStep(id); });
	r.ramp = new QTimer(this);
	r.ramp->setInterval(40);
	connect(r.ramp, &QTimer::timeout, this, [this, id]() { rampTick(id); });

	emit runStateChanged(id, true, -1);
	r.timer->start(0);
}

void PTZSequenceEngine::teardown(Runner &r)
{
	if (r.timer) {
		r.timer->stop();
		r.timer->deleteLater();
		r.timer = nullptr;
	}
	if (r.ramp) {
		r.ramp->stop();
		r.ramp->deleteLater();
		r.ramp = nullptr;
	}
	/* Stop devices that may still be moving because of this sequence */
	for (auto devId : r.motion)
		callDeviceById(devId, "ptz_stop");
	r.motion.clear();
}

void PTZSequenceEngine::stop(const QString &id)
{
	m_cancelGen[id]++;
	auto it = m_runners.find(id);
	if (it == m_runners.end())
		return;
	teardown(*it);
	m_runners.erase(it);
	emit runStateChanged(id, false, -1);
	resumeWaiters(id);
}

void PTZSequenceEngine::stopAll()
{
	for (auto id : m_runners.keys())
		stop(id);
}

void PTZSequenceEngine::finish(const QString &id, bool natural)
{
	auto it = m_runners.find(id);
	if (it == m_runners.end())
		return;
	endRamp(*it);
	teardown(*it);
	m_runners.erase(it);
	emit runStateChanged(id, false, -1);
	resumeWaiters(id);
	if (natural)
		fireTriggers(PTZTriggerType::SequenceFinished, QString(), id);
}

void PTZSequenceEngine::resumeWaiters(const QString &childId)
{
	for (auto id : m_runners.keys()) {
		auto it = m_runners.find(id);
		if (it == m_runners.end() || it->waitingFor != childId)
			continue;
		it->waitingFor.clear();
		int delay = 0;
		if (it->step >= 0 && it->step < it->seq.steps.size())
			delay = std::max(0, it->seq.steps[it->step].delayMs);
		it->timer->start(delay);
	}
}

static double ease(PTZEasing e, double t)
{
	t = std::clamp(t, 0.0, 1.0);
	if (e == PTZEasing::EaseInOut)
		return t * t * (3.0 - 2.0 * t); /* smoothstep */
	return t;
}

void PTZSequenceEngine::rampTick(const QString &id)
{
	auto it = m_runners.find(id);
	if (it == m_runners.end())
		return;
	Runner &r = *it;
	if (r.rampIdx < 0 || r.rampIdx >= r.seq.steps.size()) {
		r.ramp->stop();
		return;
	}
	const PTZSeqStep &s = r.seq.steps[r.rampIdx];
	double dur = std::max(1, s.durationMs);
	double t = std::min(1.0, r.rampClock.elapsed() / dur);
	double e = ease(s.easing, t);
	sendMove(s, s.pan + (s.pan2 - s.pan) * e, s.tilt + (s.tilt2 - s.tilt) * e,
		 s.zoom + (s.zoom2 - s.zoom) * e);
	if (auto d = resolveDevice(s))
		r.motion.insert(d->getId());
	if (t >= 1.0)
		endRamp(r);
}

/* Finish an active ramp: apply the end speeds and optionally stop the device */
void PTZSequenceEngine::endRamp(Runner &r)
{
	if (r.rampIdx < 0 || r.rampIdx >= r.seq.steps.size()) {
		r.rampIdx = -1;
		return;
	}
	const PTZSeqStep s = r.seq.steps[r.rampIdx];
	r.rampIdx = -1;
	if (r.ramp)
		r.ramp->stop();
	if (s.stopAtEnd) {
		sendMove(s, 0, 0, 0);
		if (auto d = resolveDevice(s))
			r.motion.remove(d->getId());
	} else {
		sendMove(s, s.pan2, s.tilt2, s.zoom2);
	}
}

void PTZSequenceEngine::runStep(const QString &id)
{
	auto it = m_runners.find(id);
	if (it == m_runners.end())
		return;

	{
		Runner &r = *it;
		endRamp(r); /* make sure a previous ramp is completed before moving on */

		r.step++;
		if (r.step >= r.seq.steps.size()) {
			r.loop++;
			if (r.seq.loops != 0 && r.loop >= r.seq.loops) {
				finish(id, true);
				return;
			}
			r.step = 0;
		}
		emit runStateChanged(id, true, r.step);
	}

	/* Take a copy of the step: actions below may modify m_runners */
	const PTZSeqStep s = m_runners[id].seq.steps[m_runners[id].step];
	const bool lastStep = (m_runners[id].step == m_runners[id].seq.steps.size() - 1);
	const bool endless = (m_runners[id].seq.loops == 0);
	int next = std::max(0, s.delayMs);
	bool waitChild = false;

	switch (s.type) {
	case PTZStepType::MoveAbs: {
		calldata cd = {};
		calldata_set_float(&cd, "pan", s.pan);
		calldata_set_float(&cd, "tilt", s.tilt);
		callStepDevice(s, "ptz_move_abs", &cd);
		calldata_free(&cd);
		break;
	}
	case PTZStepType::ZoomAbs: {
		calldata cd = {};
		calldata_set_float(&cd, "zoom", s.zoom);
		callStepDevice(s, "ptz_move_abs", &cd);
		calldata_free(&cd);
		break;
	}
	case PTZStepType::MoveRamp: {
		Runner &r = m_runners[id];
		r.rampIdx = r.step;
		r.rampClock.restart();
		sendMove(s, s.pan, s.tilt, s.zoom);
		if (auto d = resolveDevice(s))
			r.motion.insert(d->getId());
		r.ramp->start();
		next += std::max(0, s.durationMs);
		break;
	}
	case PTZStepType::PresetRecall: {
		calldata cd = {};
		calldata_set_int(&cd, "preset_id", s.preset);
		callStepDevice(s, "ptz_preset_recall", &cd);
		calldata_free(&cd);
		break;
	}
	case PTZStepType::Home:
		callStepDevice(s, "ptz_home_recall");
		break;
	case PTZStepType::Stop:
		callStepDevice(s, "ptz_stop");
		if (auto d = resolveDevice(s))
			m_runners[id].motion.remove(d->getId());
		break;
	case PTZStepType::Wait:
		next += std::max(0, s.durationMs);
		break;
	case PTZStepType::SwitchScene: {
		OBSSourceAutoRelease src = obs_get_source_by_name(QT_TO_UTF8(s.scene));
		if (src)
			obs_frontend_set_current_scene(src);
		else
			blog(LOG_WARNING, "sequence: scene '%s' not found", QT_TO_UTF8(s.scene));
		break;
	}
	case PTZStepType::StartSequence:
		if (s.sequenceId != id) {
			start(s.sequenceId);
			waitChild = s.waitFinish && m_runners.contains(s.sequenceId);
			if (waitChild && m_runners.contains(id))
				m_runners[id].waitingFor = s.sequenceId;
		}
		break;
	case PTZStepType::StopSequence:
		if (s.sequenceId != id)
			stop(s.sequenceId);
		else {
			stop(id);
			return;
		}
		break;
	}

	auto again = m_runners.find(id);
	if (again == m_runners.end())
		return; /* stopped from within the step */
	if (waitChild)
		return; /* resumed by resumeWaiters() */

	/* Never spin faster than 50 ms through an endless loop of instant steps */
	if (lastStep && endless)
		next = std::max(next, 50);
	again->timer->start(next);
}

/* ---------------------------------------------------------------------- */
/* Module entry points                                                    */
/* ---------------------------------------------------------------------- */

extern "C" void ptz_load_sequences(void)
{
	PTZSequenceEngine::create();
}

extern "C" void ptz_unload_sequences(void)
{
	PTZSequenceEngine::destroy();
}
