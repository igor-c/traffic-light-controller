#ifndef MYTASK_H
#define MYTASK_H

//#include <QDebug>
#include <QObject>
//#include <QRunnable>
#include "mainwindow.h"
#include <QTcpSocket>
//#include "myriddle.h"
//#include <QStringList>

class MyTask : public QObject, public QRunnable {
  Q_OBJECT
public:
  MyTask();
  void doWork(QTcpSocket *, QByteArray);
  QByteArray buf;
  QTcpSocket *p_cl;
signals:
  void Result();
  void bind_dev_name_ip(QTcpSocket *, QStringList);
  void new_data_from_client(QTcpSocket *, QStringList);
  void newParsedMsg(QTcpSocket *, QByteArray);

protected:
  void run();

private:
};

#endif // MYTASK_H
