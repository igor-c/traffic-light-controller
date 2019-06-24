#include "QMessageBox"
#include "application.h"
#include "mainwindow.h"
#include <QApplication>

MainWindow *pw;

int main(int argc, char *argv[]) {
  Application a(argc, argv);
  if (!a.lock()) {
    QMessageBox msgBox;
    msgBox.setText("You already have an instance of this beautifully application open.");
    msgBox.setInformativeText("Please close it before lounch another one.");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
    return -42;
  }
  pw = new MainWindow;
  QFile f(":qdarkstyle/style.qss");
  if (!f.exists()) {
    printf("Unable to set stylesheet, file not found\n");
  } else {
    f.open(QFile::ReadOnly | QFile::Text);
    QTextStream ts(&f);
    a.setStyleSheet(ts.readAll());
  }
  pw->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  pw->show();
  // mainWindow->move (0,0);
  // mainWindow->resize(800,900);
  return a.exec();
}
