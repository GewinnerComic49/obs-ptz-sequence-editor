/* Pan Tilt Zoom Controls - Sequence engine
 *
 * Programmable motion sequences for PTZ devices that can be started manually
 * or by triggers (scene changes, time of day, intervals, end of another
 * sequence, streaming/recording events, OBS start).
 *
 * SPDX-License-Identifier: GPLv2
 */
#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QTime>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QJsonObject>
#include <obs-frontend-api.h>

enum class PTZStepType {
	MoveAbs,       /* absolute pan/tilt position */
	ZoomAbs,       /* absolute zoom position */
	MoveRamp,      /* timed continuous move with speed ramp from start to end speeds */
	PresetRecall,  /* recall a camera preset */
	Home,          /* go to home position */
	Stop,          /* stop all movement of a device */
	Wait,          /* pause */
	SwitchScene,   /* change the OBS program scene */
	StartSequence, /* start another sequence (optionally wait for it) */
	StopSequence,  /* stop another sequence */
};

enum class PTZEasing { Linear, EaseInOut };

enum class PTZTriggerType {
	SceneActive,      /* scene becomes program scene */
	SceneInactive,    /* scene leaves program */
	TimeOfDay,        /* at a given time on selected weekdays */
	Interval,         /* every N seconds */
	SequenceFinished, /* another sequence ended on its own */
	StreamStarted,
	StreamStopped,
	RecordingStarted,
	RecordingStopped,
	ObsStarted,
};

struct PTZSeqStep {
	PTZStepType type = PTZStepType::Wait;
	uint32_t deviceId = 0;
	QString deviceName;
	/* MoveAbs: target pan/tilt. ZoomAbs: target zoom. MoveRamp: start speeds */
	double pan = 0.0, tilt = 0.0, zoom = 0.0;
	/* MoveRamp: end speeds */
	double pan2 = 0.0, tilt2 = 0.0, zoom2 = 0.0;
	PTZEasing easing = PTZEasing::Linear;
	bool stopAtEnd = true; /* MoveRamp: stop device when ramp is done */
	int durationMs = 1000; /* MoveRamp, Wait */
	int delayMs = 0;       /* pause after the step */
	int preset = 1;
	QString scene;
	QString sequenceId;
	bool waitFinish = false; /* StartSequence: continue only when child finished */

	int totalMs() const;
	QJsonObject toJson() const;
	static PTZSeqStep fromJson(const QJsonObject &o);
};

struct PTZSeqTrigger {
	PTZTriggerType type = PTZTriggerType::SceneActive;
	QString scene;             /* scene triggers; empty == any scene */
	QTime time{12, 0, 0};      /* TimeOfDay */
	int weekdays = 0x7F;       /* TimeOfDay, bit0 = Monday ... bit6 = Sunday */
	int intervalSec = 60;      /* Interval */
	QString sequenceId;        /* SequenceFinished */
	int delayMs = 0;           /* delay between trigger and start */
	bool stopOnLeave = false;  /* SceneActive: stop the sequence when scene is left */

	QJsonObject toJson() const;
	static PTZSeqTrigger fromJson(const QJsonObject &o);
};

struct PTZSequence {
	QString id;
	QString name;
	bool enabled = true;
	int loops = 1;                /* 0 == endless */
	bool restartOnTrigger = false; /* restart instead of ignoring a trigger while running */
	QList<PTZSeqStep> steps;
	QList<PTZSeqTrigger> triggers;

	int totalMs() const;
	QJsonObject toJson() const;
	static PTZSequence fromJson(const QJsonObject &o);
};

QString ptzStepTypeKey(PTZStepType t);
QString ptzTriggerTypeKey(PTZTriggerType t);
QString ptzStepTypeName(PTZStepType t);
QString ptzTriggerTypeName(PTZTriggerType t);

class PTZSequenceEngine : public QObject {
	Q_OBJECT

public:
	static PTZSequenceEngine *instance();
	static void create();
	static void destroy();

	QList<PTZSequence> &sequences() { return m_sequences; }
	PTZSequence *find(const QString &id);
	QString add(const QString &name);
	QString duplicate(const QString &id);
	void remove(const QString &id);
	/* Call after editing a sequence in place: schedules save and re-arms triggers */
	void changed();

	bool isRunning(const QString &id) const { return m_runners.contains(id); }
	int runningStep(const QString &id) const;
	QStringList runningIds() const { return m_runners.keys(); }

	void load();
	void save();

public slots:
	void start(const QString &id);
	void stop(const QString &id);
	void stopAll();

signals:
	void listChanged();
	void runStateChanged(const QString &id, bool running, int step);

private:
	PTZSequenceEngine();
	~PTZSequenceEngine();

	struct Runner {
		PTZSequence seq;
		int step = -1;
		int loop = 0;
		QTimer *timer = nullptr;
		QTimer *ramp = nullptr;
		QElapsedTimer rampClock;
		int rampIdx = -1;
		QSet<uint32_t> motion; /* devices that may still be moving */
		QString waitingFor;
	};

	static void onFrontendEvent(enum obs_frontend_event event, void *ptr);
	void handleFrontendEvent(enum obs_frontend_event event);
	void fireTriggers(PTZTriggerType type, const QString &scene = QString(),
			  const QString &sequenceId = QString());
	void fire(const QString &seqId, int delayMs);
	void onTick();

	void runStep(const QString &id);
	void rampTick(const QString &id);
	void endRamp(Runner &r);
	void finish(const QString &id, bool natural);
	void teardown(Runner &r);
	void resumeWaiters(const QString &childId);

	QList<PTZSequence> m_sequences;
	QMap<QString, Runner> m_runners;
	QHash<QString, quint64> m_cancelGen;
	QHash<QString, qint64> m_intervalDue;
	QElapsedTimer m_mono;
	QDateTime m_lastTick;
	QTimer m_tickTimer;
	QTimer m_saveTimer;
	QString m_lastScene;
	QString m_path;
	bool m_loaded = false;

	static PTZSequenceEngine *s_instance;
};
