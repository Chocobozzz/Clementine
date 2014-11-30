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

#ifndef QOBUZSETTINGSPAGE_H
#define QOBUZSETTINGSPAGE_H

#include "ui/settingspage.h"

class Ui_QobuzSettingsPage;
class QobuzService;

class QobuzSettingsPage : public SettingsPage {
  Q_OBJECT

 public:
  QobuzSettingsPage(SettingsDialog* parent = nullptr);
  ~QobuzSettingsPage();

  void Load();
  void Save();

 private slots:
  void LoginClicked();
  void LogoutClicked();
  void Connected();
  void NotPremium();
  void ReplyError(int status_code);

 private:
  Ui_QobuzSettingsPage *ui_;

  QobuzService* service_;
};

#endif  // QOBUZSETTINGSPAGE_H
