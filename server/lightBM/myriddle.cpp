#include "myriddle.h"
#include "mainwindow.h"
#include "myserver.h"

MyRiddle::MyRiddle(QObject *parent) : QObject(parent) {
  int cnt = sizeof dev / sizeof dev[0];
  for (int i = 0; i < cnt; i++) {
    dev[i].GroupName = riddleList[i][0];
    dev[i].SubGroupName = riddleList[i][1];
  }

  int cnt0 = 0;
  quint16 size = 1024;
  ledData.append(quint8((size >> 8) & 0xff));
  ledData.append(quint8(size & 0xff));
  ledData.append(quint8(2));

  for (int i = 0; i < size - 1; i++) {
    ledData.append(quint8(cnt0));
    cnt0++;
    if (cnt0 > 254) {
      cnt0 = 0;
    }
  }

  int ind = 1000;
  qDebug() << "ind of resp val: " << ind << "; Hex val: " << QString::number(static_cast<quint8>(ledData.at(ind + 2)));
}

void MyRiddle::newParsedMsg(QTcpSocket *cl, QByteArray buf) {
  if (buf.count() > 0) {
    // 1."trafic light"
    if (static_cast<int>(buf.at(0) == 0 && static_cast<int>(buf.at(1) >= 0))) {
      int cnt = sizeof dev / sizeof dev[0];
      for (int i = 0; i < cnt; ++i) {
        if (dev[i].GroupName == GroupNamelist[0] && dev[i].SubGroupName == SubGroupNamelist[buf.at(1)]) {
          qDebug() << "get: " << i;
          dev[i].ip = QHostAddress(cl->peerAddress());
          qDebug() << dev[i].ip;
          dev[i].descriptor = cl->socketDescriptor();
          dev[i].state = "online";
          dev[i].connectTime = QDateTime::currentDateTime();
          emit new_autorization();
        }
      }
    }
    // DATA
    else if (static_cast<int>(buf.at(0) == 3)) {
      // BUTTON
      if (static_cast<int>(buf.at(1) == 1)) {
        extern MainWindow *pw;
        connect(this, SIGNAL(TLbtnPresed(QString, QString, int)), pw, SLOT(onTLbtnPresed(QString, QString, int)), Qt::UniqueConnection);
        int cnt = sizeof dev / sizeof dev[0];
        for (int i = 0; i < cnt; ++i) {
          if (cl->peerAddress() == dev[i].ip) {
            if (static_cast<int>(buf.at(2) == 1)) {
              emit TLbtnPresed(dev[i].GroupName, dev[i].SubGroupName, 1);
            }
          }
        }
      }
    }

    //    else {
    //      extern MainWindow *pw;
    //      connect(this, SIGNAL(updateTableWidgetTarget(QString, QStringList)), pw, SLOT(onUpdateTableWidgetTarget(QString, QStringList)),
    //              Qt::UniqueConnection);
    //      // connect(this, SIGNAL(  change_rid_state(QString,QString,QString)),      pw, SLOT(   onChange_rid_state(QString,QString,QString)),
    //      // Qt::UniqueConnection);
    //      connect(this, SIGNAL(send_data(QString, QString, QString)), pw, SLOT(send_by_name(QString, QString, QString)), Qt::UniqueConnection);
    //      connect(this, SIGNAL(send_data(QString, QString, QByteArray)), pw, SLOT(send_by_name(QString, QString, QByteArray)),
    //      Qt::UniqueConnection);

    //      int cnt = sizeof dev / sizeof dev[0];
    //      for (int i = 0; i < cnt; ++i) {
    //        if (cl->peerAddress() == dev[i].ip) {
    //          //          if (static_cast<int>(buf.at(0) == 2)) {
    //          //            emit send_data(dev[i].GroupName, dev[i].SubGroupName, ledData);
    //          //          }
    //        }
    //      }
    //    }
  }
}
void MyRiddle::_unbind_dev_ip(QHostAddress ip) {
  int cnt = sizeof dev / sizeof dev[0];
  for (int i = 0; i < cnt; ++i) {
    if (dev[i].ip == ip) {
      dev[i].ip = nullptr;
      dev[i].descriptor = 0;
    }
  }
  // emit new_autorization();
}
