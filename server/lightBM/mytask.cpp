#include "mytask.h"
#include "QDebug"
#include "myriddle.h"

MyTask::MyTask() {}

void MyTask::run() { doWork(p_cl, buf); }
void MyTask::doWork(QTcpSocket *cl, QByteArray buf) {
  extern MainWindow *pw;
  QObject::connect(this, &MyTask::newParsedMsg, pw, &MainWindow::_newParsedMsg);
  emit newParsedMsg(cl, buf);

  //    QStringList query = cmd.split("#\r\n");

  //    for (int i = 0; i < query.size(); ++i){
  //        qDebug()<<query.at(i);
  //    }

  //    for (int i = 0; i < query.size(); ++i){
  //        if(query.at(i)!=""){
  //            QStringList subQuery = query.at(i).split("*");
  //            extern MainWindow *pw;
  //            QObject::connect(this, &MyTask::newParsedMsg, pw, &MainWindow::_newParsedMsg);
  //            subQuery.removeFirst();
  //            emit newParsedMsg(cl, subQuery);
  //        }
  //    }

  //    if(query.at(0)=="TARGETID"){
  //        extern MainWindow *pw;
  //        if(query.at(1)!=""){
  //            QObject::connect(this, &MyTask::bind_dev_name_ip, pw, &MainWindow::_bind_dev_name_ip);
  //            emit bind_dev_name_ip(cl, query);
  //        }
  //        else{
  //            QObject::connect(this, &MyTask::bind_dev_name_ip, pw, &MainWindow::_bind_dev_name_ip);
  //            emit bind_dev_name_ip(cl, query);
  //        }
  //    }
  //    else if(query.at(0)=="DATA" || query.at(0)=="STATE"){
  //        extern MainWindow *pw;
  //        QObject::connect(this, &MyTask::new_data_from_client, pw, &MainWindow::_new_data_from_client);
  //        emit new_data_from_client(cl, query);
  //    }

  // emit Result();
}
