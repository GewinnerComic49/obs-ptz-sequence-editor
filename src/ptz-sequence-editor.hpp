/* Pan Tilt Zoom Controls - Sequence editor window
 *
 * SPDX-License-Identifier: GPLv2
 */
#pragma once

#include <QDialog>
#include <QString>

class QListWidget;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QComboBox;
class QTableWidget;
class QPushButton;
class QLabel;
struct PTZSequence;

class PTZSequenceEditor : public QDialog {
	Q_OBJECT

public:
	static void showEditor();
	static void destroyEditor();

private:
	explicit PTZSequenceEditor(QWidget *parent);
	~PTZSequenceEditor();

	static PTZSequenceEditor *s_instance;

	QListWidget *seqList;
	QLineEdit *nameEdit;
	QCheckBox *enabledCheck;
	QSpinBox *loopsSpin;
	QComboBox *retriggerCombo;
	QTableWidget *stepTable;
	QTableWidget *trigTable;
	QPushButton *runBtn;
	QPushButton *stopBtn;
	QLabel *statusLabel;
	QWidget *detailPane;

	QString currentId;
	bool loading = false;

	PTZSequence *cur();
	void refreshList(const QString &select = QString());
	void loadCurrent();
	void refreshSteps();
	void refreshTriggers();
	void updateRunState();
	void commit();

	void addStep();
	void editStep();
	void duplicateStep();
	void removeStep();
	void moveStep(int delta);
	void addTrigger();
	void editTrigger();
	void removeTrigger();

private slots:
	void onSeqSelectionChanged();
};
