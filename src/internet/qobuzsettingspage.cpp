/* This file is part of Clementine.
   Copyright 2014, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "qobuzservice.h"
#include "qobuzsettingspage.h"
#include "ui_qobuzsettingspage.h"
#include "core/application.h"
#include "internet/internetmodel.h"

QobuzSettingsPage::QobuzSettingsPage(SettingsDialog* parent)
  : SettingsPage(parent),
    ui_(new Ui_QobuzSettingsPage),
    service_(
      dialog()->app()->internet_model()->Service<QobuzService>()) {
  ui_->setupUi(this);
  ui_->login_state->AddCredentialGroup(ui_->login_container);

  connect(ui_->login_button, SIGNAL(clicked()), SLOT(LoginClicked()));
  connect(ui_->login_state, SIGNAL(LogoutClicked()), SLOT(LogoutClicked()));
  connect(service_, SIGNAL(Connected()), SLOT(Connected()));
  connect(service_, SIGNAL(ReplyError(int)), SLOT(ReplyError(int)));
  connect(service_, SIGNAL(NotPremium()), SLOT(NotPremium()));

}

QobuzSettingsPage::~QobuzSettingsPage() { delete ui_; }

void QobuzSettingsPage::Load() {
  QSettings s;
  s.beginGroup(QobuzService::kSettingsGroup);

  QString username = s.value("username").toString();
  ui_->username->setText(username);

  if(service_->IsLoggedIn()) {
    Connected();
  }
}

void QobuzSettingsPage::Save() {
  int quality_box_index = ui_->quality_box->currentIndex();
  QVariant quality_variant = ui_->quality_box->itemData(quality_box_index);

  QSettings s;
  s.beginGroup(QobuzService::kSettingsGroup);

  s.setValue("username", ui_->username->text());

  if(quality_variant.isValid()) {
    int quality = quality_variant.toInt();
    s.setValue("quality", quality);
    service_->set_quality(QobuzService::Quality(quality));
  }
}

void QobuzSettingsPage::LoginClicked() {
  if (service_->IsLoggedIn()) {
    return;
  }

  ui_->login_state->SetLoggedIn(LoginStateWidget::LoginInProgress);
  service_->Connect(ui_->username->text(), ui_->password->text());
}

void QobuzSettingsPage::LogoutClicked() {
  service_->Logout();
  ui_->login_button->setEnabled(true);
  ui_->login_state->SetLoggedIn(LoginStateWidget::LoggedOut);
  ui_->username->clear();
  ui_->password->clear();
  ui_->quality_box->clear();

  QSettings s;
  s.beginGroup(QobuzService::kSettingsGroup);
  s.remove("hifi_subscriber");
  s.remove("username");
  s.remove("user_mail");
}

void QobuzSettingsPage::Connected() {
  QSettings s;
  s.beginGroup(QobuzService::kSettingsGroup);

  QString user_mail = s.value("user_mail").toString();
  bool hifi_subscriber = s.value("hifi_subscriber").toBool();

  ui_->login_state->SetAccountTypeVisible(false);

  // Information of the account type
  QString type_account = hifi_subscriber ? "%1 (Qobuz Hi-Fi)" : "%1 (Qobuz Premium)";
  ui_->login_state->SetLoggedIn(LoginStateWidget::LoggedIn, type_account.arg(user_mail));

  // Build the quality box if needed
  if(ui_->quality_box->count() == 0) {
    ui_->quality_box->addItem("MP3", QobuzService::Quality::MP3);

    if(hifi_subscriber) {
      ui_->quality_box->addItem("FLAC", QobuzService::Quality::FLAC);
    }
  }

  // Set the quality saved previously ?
  int index = ui_->quality_box->findData(s.value("quality"));
  if(index != -1) {
    ui_->quality_box->setCurrentIndex(index);
  }

}

void QobuzSettingsPage::ReplyError(int status_code) {
  if(status_code == QobuzService::Error::UNAUTHORIZED) {
    ui_->login_button->setEnabled(true);
    ui_->login_state->SetLoggedIn(LoginStateWidget::LoggedOut);

    ui_->login_state->SetAccountTypeVisible(true);
    ui_->login_state->SetAccountTypeText(tr("Your username or password was incorrect."));
  }
}

void QobuzSettingsPage::NotPremium() {
  ui_->login_button->setEnabled(true);
  ui_->login_state->SetLoggedIn(LoginStateWidget::LoggedOut);

  ui_->login_state->SetAccountTypeVisible(true);
  ui_->login_state->SetAccountTypeText(tr("You don't have a Qobuz Premium account."));
}
