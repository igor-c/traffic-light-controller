#include "dialog.h"
#include "QDebug"
#include "ui_dialog.h"

Dialog::Dialog(QWidget *parent) : QDialog(parent), ui(new Ui::Dialog) {
  ui->setupUi(this);
  // setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowTitleHint);
}

Dialog::~Dialog() { delete ui; }

void Dialog::on_pushButton_clicked() { this->close(); }
void Dialog::on_pushButton_2_clicked() { this->close(); }
void Dialog::set_btnEnable(QString name, bool state) {
  QPushButton *btn = new QPushButton();
  if (name == "CANCEL") {
    btn = ui->pushButton_2;
  } else if (name == "OK") {
    btn = ui->pushButton;
  }
  btn->setEnabled(state);
}
void Dialog::set_text(QString str) { ui->plainTextEdit->appendPlainText(str); }
