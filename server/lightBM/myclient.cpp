#include "myclient.h"
#include "mainwindow.h"
#include "myserver.h"
#include "mytask.h"

//#include <sys/types.h>
//#include <sys/socket.h>

#ifdef _WIN32
#include <Ws2tcpip.h>
#include <stdio.h>
#include <winsock2.h>
#elif __APPLE__
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

MyClient::MyClient(QObject *parent) : QObject(parent) {
  QThreadPool::globalInstance()->setMaxThreadCount(15);

  //  extern MainWindow *pw;
  //  if (pw->KeepAliveEnable) {
  //    timerKA = new QTimer(this);
  //    timerKA->start(pw->freqKeepAliveReq);
  //    connect(timerKA, SIGNAL(timeout()), this, SLOT(timer_update()));
  //    cntKE = pw->numKeepAliveReq;
  //  }
}
void MyClient::timer_update() {
  //  if (cntKE > 0)
  //    cntKE--;
  //  else {
  //    emit disconnected();
  //  }
}
void MyClient::SetSocket(qintptr Descriptor) {
  socket = new QTcpSocket(this);
  socket->setSocketDescriptor(Descriptor);
  socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
  size = 0;

  //    int keepalive_enabled = 1;
  int keepalive_time = 5;
  int keepalive_count = 3;
  int keepalive_interval = 1;
  int socket_file_descriptor = static_cast<int>(socket->socketDescriptor());
  //    setsockopt(socket_file_descriptor, SOL_SOCKET, SO_KEEPALIVE,  &keepalive_enabled , sizeof(keepalive_enabled ));
#ifdef _WIN32
  //        setsockopt(socket_file_descriptor, SOL_SOCKET, TCP_KEEPIDLE,  &keepalive_time, sizeof keepalive_time);
  //        setsockopt(socket_file_descriptor, SOL_SOCKET, TCP_KEEPCNT,   &keepalive_count, sizeof keepalive_count);
  //        setsockopt(socket_file_descriptor, SOL_SOCKET, TCP_KEEPINTVL, &keepalive_interval, sizeof keepalive_interval);
#elif __APPLE__
  setsockopt(socket_file_descriptor, IPPROTO_TCP, TCP_KEEPALIVE, &keepalive_time, sizeof keepalive_time);
  setsockopt(socket_file_descriptor, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_count, sizeof keepalive_count);
  setsockopt(socket_file_descriptor, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_interval, sizeof keepalive_interval);
#endif

  //    extern MainWindow *pw;
  //    if(pw->OStype=="windows"){
  //    }
  //    else if(pw->OStype=="osx" || pw->OStype=="darwin"){
  //    }

  connect(socket, SIGNAL(connected()), this, SLOT(connected()));
  connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
  connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
  connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(error(QAbstractSocket::SocketError)));
  connect(socket, &QAbstractSocket::disconnected, this, &QObject::deleteLater);

  emit addClient(socket);
}

QString MyClient::get_os() {
  enum OperatingSystem { OS_WINDOWS, OS_UNIX, OS_LINUX, OS_MAC };

#if (defined(Q_OS_WIN) || defined(Q_OS_WIN32) || defined(Q_OS_WIN64))
  OperatingSystem os = OS_WINDOWS;
#elif (defined(Q_OS_UNIX))
  OperatingSystem os = OS_UNIX;
#elif (defined(Q_OS_LINUX))
  OperatingSystem os = OS_LINUX;
#elif (defined(Q_OS_MAC))
  OperatingSystem os = OS_MAC;
#endif

  // qDebug() << "OS: " << os;

  switch (os) {
  case OS_WINDOWS:
    return "Windows";
  case OS_UNIX:
    return "Unix";
  case OS_LINUX:
    return "Linux";
  case OS_MAC:
    return "Mac";
  default:
    return "Unknown";
  }
}
void MyClient::error(QAbstractSocket::SocketError error) { qDebug() << "Socket error" << error; }
void MyClient::connected() {
  //    socket->setSocketOption(QAbstractSocket:: KeepAliveOption, 1);
}

void MyClient::disconnected() {
  // socket->deleteLater();
  size = 0;
  buffer.clear();
  qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss:zzz") << socket->peerAddress().toString().mid(0) << "client disconnected";
  emit delClient(socket);
  socket->deleteLater();
}

void MyClient::readyRead() {
  extern MainWindow *pw;
  cntKE = pw->numKeepAliveReq;
  while (socket->bytesAvailable() > 0) {
    buffer.append(socket->readAll());
    qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss:zzz") << socket->peerAddress().toString().mid(0) << buffer;
    /*
         (size == 0 && buffer->size() >= 4) если еще не получили данные о размере длины посылки (длина 4 байта)
         (size > 0 && buffer->size() >= size) если получили размер посылки но не получили всю посылку
    */
    while ((size == 0 && buffer.size() >= 2) || (size > 0 && buffer.size() >= size)) // While can process data, process it
    {
      if (size == 0 && buffer.size() >= 2) // if size of data has received completely, then store it on our global variable
      {
        size = ArrayToInt(buffer.mid(0, 2));
        // qDebug() <<"1. GET SIZE: "<<size;
        buffer.remove(0, 2);
      }
      if (size > 0 && buffer.size() >= size) // If data has received completely, then emit our SIGNAL with the data
      {
        QByteArray data = buffer.mid(0, size);
        buffer.remove(0, size);
        size = 0;
        // qDebug() <<"2. GET MSG: "<<data;

        quint8 array[data.size()];
        memcpy(array, data.data(), data.count());
        QString strData;
        for (int i = 0; i < data.count(); i++) {
          strData.append(QString::number((quint8)data.at(i)).append(" "));
        }
        QString debStr = "⇦ " + socket->peerAddress().toString().mid(0) + ": " + strData;
        emit sPrintToConsol(debStr, 2);

        TaskStart(data);
      }
    }
  }

  //QString debStr= "⇦ "+socket->peerAddress().toString().mid(0)+" "+data;
  //    emit sPrintToConsol(debStr, 2);h
  //    TaskStart(data);
}

quint16 MyClient::ArrayToInt(QByteArray source) {
  quint16 temp;
  QDataStream data(&source, QIODevice::ReadWrite);
  data >> temp;
  return temp;
}

void MyClient::TaskStart(QByteArray data) {
  MyTask *mytask = new MyTask();
  mytask->setAutoDelete(true);
  connect(mytask, SIGNAL(Result()), SLOT(TaskResult()), Qt::QueuedConnection);
  mytask->buf = data;
  mytask->p_cl = socket;
  QThreadPool::globalInstance()->start(mytask);
}

void MyClient::sendData(QTcpSocket *cl, QString cmd) {
  QByteArray Buffer;
  Buffer.append(cmd + "\r\n");
  int num_bytes_write = cl->write(Buffer);

  //    QString debStr;
  //    debStr= socket->peerAddress().toString().mid(0)
  //           +" send data: "
  //           +cmd;
  //    emit sPrintToConsol(debStr, 2);

  //    qDebug()<<QDateTime::currentDateTime().toString("hh:mm:ss:zzz")
  //            <<cl->peerAddress().toString()
  //            <<"send data: "
  //            <<cmd
  //            <<" were_actually_written: "
  //            <<QString::number(num_bytes_write);
}

void MyClient::TaskResult() {
  //    extern MainWindow *pw;
  //    connect(this, &MyClient::new_autorization, pw, &MyServer::on_new_autorization);
  //    emit new_autorization();

  //    int cnt = sizeof riddle::dev / sizeof riddle::dev[0];
  //    for (int i = 0; i < cnt; ++i){
  //        qDebug()<<riddle::dev[i].NodeName;
  //                <<" "
  //                <<riddle::dev[i].name
  //                <<" "
  //                <<riddle::dev[i].ip;<<riddle::dev[i].ip;
  //    }

  //    QByteArray Buffer;
  //    Buffer.append("START\r\n");
  //    socket->write(Buffer);

  //    QDateTime t = QDateTime::currentDateTime ();
  //    QString s = t.toString("hh:mm:ss:zzz");
  //    qDebug()<<s<<": "<<socket->peerAddress().toString()<<" - "<<"sent request";
}
