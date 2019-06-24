#ifndef MYCLIENT_H
#define MYCLIENT_H

//#include <QObject>
#include <QDebug>
#include <QTcpSocket>
#include <QThreadPool>
//#include <QAbstractSocket>
//#include <QList>
//#include <QtNetwork>
#include <QDateTime>
#include <QTimer>

class MyClient : public QObject {
  Q_OBJECT
public:
  explicit MyClient(QObject *parent = nullptr);
  void SetSocket(qintptr Descriptor);

signals:
  void addClient(QTcpSocket *);
  void delClient(QTcpSocket *);
  void new_autorization();
  void sPrintToConsol(QString, int);

public slots:
  void timer_update();
  void error(QAbstractSocket::SocketError error);
  void connected();
  void disconnected();
  void readyRead();
  void TaskResult();
  void TaskStart(QByteArray);
  void sendData(QTcpSocket *, QString);
  quint16 ArrayToInt(QByteArray source);

private:
  QTimer *timerKA;
  int cntKE;
  QString get_os();
  QTcpSocket *socket;
  QByteArray buffer;
  qint32 size;
  //    QByteArray tempBuf;
  //    QHash<QTcpSocket*, QByteArray*> buffers; //We need a buffer to store data until block has completely received
  //    QHash<QTcpSocket*, qint32*> sizes; //We need to store the size to verify if a block has received completely
};

#endif // MYCLIENT_H
