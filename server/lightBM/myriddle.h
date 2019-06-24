#ifndef MYRIDDLE_H
#define MYRIDDLE_H

//#include <QObject>
#include <QDateTime>
#include <QHostAddress>
//#include <QStringList>
#include <QTcpSocket>
//#include <qabstractsocket.h>
//#include <QStringList>
//#include "myserver.h"
//#include "mainwindow.h"

class MyRiddle : public QObject {
  Q_OBJECT
public:
  explicit MyRiddle(QObject *parent = 0);
  // MainWindow *mw;

private:
  QByteArray ledData;

public:
  void play_bad_sound(QString area, QString name);
  // void bind_dev_ip(QTcpSocket *, QStringList);
  // void data_from_client(QTcpSocket *, QStringList);
  void newParsedMsg(QTcpSocket *, QByteArray);

  typedef struct {
    QString GroupName;
    QString SubGroupName;
    QString state = "offline";
    QDateTime connectTime;
    QHostAddress ip;
    qintptr descriptor;
    QString history_massage[10][2]; // 1st collumn - time; 2rd collumn - message
  } devices;
  devices dev[4];

  // QString GroupNamelist[2] = {"system", "trafic light"};
  QString GroupNamelist[1] = {"trafic light"};
  // QString SubGroupNamelist[5] = {"power control", "1/2", "3/4", "5/6", "7/8"};
  QString SubGroupNamelist[4] = {"1/2", "3/4", "5/6", "7/8"};
  // QHash <int, QString> clNames{}
  QString riddleList[4][2] = {{GroupNamelist[0], SubGroupNamelist[0]},
                              {GroupNamelist[0], SubGroupNamelist[1]},
                              {GroupNamelist[0], SubGroupNamelist[2]},
                              {GroupNamelist[0], SubGroupNamelist[3]}};

signals:
  void TLbtnPresed(QString area, QString name, int val);
  void alarm_window();
  void start_first_room();
  void play_sound(QString);
  void new_autorization();
  void UpdateTableWidget2(QString area, QString name, bool st);
  void change_rid_state(QString area, QString name, QString st);
  void send_data(QString cur_erea, QString cur_name, QString cmd);
  void send_data(QString cur_erea, QString cur_name, QByteArray cmd);
  void updateTableWidgetTarget(QString, QStringList);

public slots:
  void _unbind_dev_ip(QHostAddress);
};

#endif // RIDDLE_H
