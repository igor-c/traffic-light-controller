#ifndef MYSERVER_H
#define MYSERVER_H

#include <QByteArray>
#include <QTcpServer>
//#include <QTcpSocket>
//#include <QAbstractSocket>
//#include "myclient.h"
//#include "mainwindow.h"
#include "myriddle.h"

class MyRiddle;

class MyServer : public QTcpServer {
  Q_OBJECT

public:
  explicit MyServer(QObject *parent = nullptr);
  void StartServer(quint16 port);
  void StopServer();
  QList<QTcpSocket *> sockets_list; // получатели данных
  QHash<QString, QByteArray> hash;

private:
  uint32_t ACK_cnt = 0;

protected:
  void incomingConnection(qintptr handle);
  void newConnection(int handle);

signals:
  void unbind_dev_ip(QHostAddress);
  void UpdateDevicesList(QList<QTcpSocket *> &);
  void client_send(QTcpSocket *, QString);
  void sPrintToConsol(QString, int);

private slots:
  void on_addClient(QTcpSocket *);
  void on_delClient(QTcpSocket *);

public slots:
  void on_SendCmdClient(const qintptr &, QByteArray);
  void on_SendCmdClient(const qintptr &, QString);
  void on_new_autorization();
};
#endif // MYSERVER_H
