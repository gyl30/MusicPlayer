#include <QMenu>
#include <QAction>
#include <QApplication>
#include "log.h"
#include "tray_icon.h"

tray_icon::tray_icon(QObject* parent) : QObject(parent) { setup_tray_icon(); }

tray_icon::~tray_icon() = default;

void tray_icon::setup_tray_icon()
{
    tray_icon_ = new QSystemTrayIcon(this);
    tray_menu_ = new QMenu();
    tray_icon_->setIcon(QApplication::windowIcon());

    show_hide_action_ = new QAction("显示/隐藏", this);
    connect(show_hide_action_, &QAction::triggered, this, [this]() { emit show_hide_triggered(); });

    quit_action_ = new QAction("退出", this);
    connect(quit_action_,
            &QAction::triggered,
            this,
            [this]()
            {
                LOG_INFO("quitting application via tray menu");
                tray_icon_->hide();
                emit quit_triggered();
            });

    tray_menu_->addAction(show_hide_action_);
    tray_menu_->addAction(quit_action_);

    tray_icon_->setContextMenu(tray_menu_);
    tray_icon_->setToolTip("Music Player");
    connect(tray_icon_, &QSystemTrayIcon::activated, this, &tray_icon::on_tray_icon_activated);
}

void tray_icon::show() { tray_icon_->show(); }

bool tray_icon::isVisible() const { return tray_icon_->isVisible(); }

void tray_icon::on_tray_icon_activated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
    {
        emit show_hide_triggered();
    }
}
