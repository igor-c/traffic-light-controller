#include "myserver.h"
#include "mainwindow.h"
#include <QHostInfo>

MyServer::MyServer(QObject *parent) : QTcpServer(parent) {

  static const char mydata[] = {'\x00', '\x02', '\x00', '\x00'};
  hash.insert("whoAreYou", QByteArray::fromRawData(mydata, sizeof(mydata)));
}

void MyServer::StartServer(quint16 port) {
  extern MainWindow *pw;
  QObject::connect(this, &MyServer::sPrintToConsol, pw, &MainWindow::printToConsol, Qt::UniqueConnection);

  QString adr = pw->cbHostAddress;
  bool listingState = false;
  //
  if (adr == "Any") {
    listingState = this->listen(QHostAddress::LocalHost, port);
  } else if (adr == "LocalHost") {
    listingState = this->listen(QHostAddress::LocalHost, port);
  } else {
    listingState = this->listen(QHostAddress(adr), port);
  }
  if (listingState) {
    emit sPrintToConsol("tcp server started", 1);
  } else {
    emit sPrintToConsol("tcp server not started", 1);
  }
}

void MyServer::StopServer() {
  ACK_cnt = 0;
  this->close();
  emit sPrintToConsol("tcp server stoped", 1);
}

void MyServer::incomingConnection(qintptr handle) {
  MyClient *client = new MyClient(this);
  extern MainWindow *pw;
  QObject::connect(this, &MyServer::UpdateDevicesList, pw, &MainWindow::on_UpdateDevicesList, Qt::UniqueConnection);
  QObject::connect(client, &MyClient::addClient, this, &MyServer::on_addClient, Qt::UniqueConnection);
  QObject::connect(client, &MyClient::delClient, this, &MyServer::on_delClient, Qt::UniqueConnection);
  QObject::connect(this, &MyServer::client_send, client, &MyClient::sendData, Qt::UniqueConnection);
  QObject::connect(client, &MyClient::sPrintToConsol, pw, &MainWindow::printToConsol, Qt::UniqueConnection);
  client->SetSocket(handle);
}

void MyServer::on_new_autorization() {
  qDebug() << "new_autorization";
  emit UpdateDevicesList(sockets_list);
}

// emit after incomingConnection
void MyServer::on_addClient(QTcpSocket *cl) {
  qDebug() << "on_addClient";
  sockets_list.append(cl);
  emit UpdateDevicesList(sockets_list);
  on_SendCmdClient(cl->socketDescriptor(), hash.value("whoAreYou"));
}

void MyServer::on_delClient(QTcpSocket *cl) {
  for (int i = 0; i < sockets_list.size(); ++i) {
    if (sockets_list.at(i)->peerAddress() == cl->peerAddress()) {
      qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss:zzz") << sockets_list.at(i)->peerAddress().toString() << "client deleted from list";
      emit unbind_dev_ip(sockets_list.at(i)->peerAddress());
      sockets_list.removeAt(i);
      break;
    }
  }
  emit UpdateDevicesList(sockets_list);
}

void MyServer::on_SendCmdClient(const qintptr &descriptor, QByteArray Buffer) {
  foreach (QTcpSocket *client, sockets_list) {
    if (client->socketDescriptor() == descriptor) {

      extern MainWindow *pw;
      qint64 num_bytes_write = client->write(Buffer);

      QString strData;
      for (int i = 0; i < Buffer.count(); i++) {
        strData.append(QString::number((quint8)Buffer.at(i)).append(" "));
      }

      QString debStr;
      debStr = "⇨ " + QString::number(num_bytes_write) + " bytes to: " + client->peerAddress().toString().mid(0) + ": " + strData;
      if (strData == "*ACK#" && pw->consolLevelOutput[0]) {
        emit sPrintToConsol(debStr, 2);
      } else {
        emit sPrintToConsol(debStr, 2);
      }
    }
  }
}

void MyServer::on_SendCmdClient(const qintptr &descriptor, QString cmd) {
  foreach (QTcpSocket *client, sockets_list) {
    if (client->socketDescriptor() == descriptor) {

      extern MainWindow *pw;
      QByteArray Buffer;
      Buffer.append(cmd + "\r\n");
      qint64 num_bytes_write = client->write(Buffer);

      QString debStr;
      debStr = "⇨ " + QString::number(num_bytes_write) + " bytes to: " + client->peerAddress().toString().mid(0) + ": " + Buffer;
      if (cmd == "*ACK#" && pw->consolLevelOutput[0]) {
        emit sPrintToConsol(debStr, 2);
      } else {
        emit sPrintToConsol(debStr, 2);
      }
    }
  }
}
