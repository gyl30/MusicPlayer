#ifndef TRAY_ICON_H
#define TRAY_ICON_H

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;
class QAction;

class tray_icon : public QObject
{
    Q_OBJECT

   public:
    explicit tray_icon(QObject* parent = nullptr);
    ~tray_icon() override;

    void show();
    bool isVisible() const;

   signals:
    void show_hide_triggered();
    void quit_triggered();

   private slots:
    void on_tray_icon_activated(QSystemTrayIcon::ActivationReason reason);

   private:
    void setup_tray_icon();

    QSystemTrayIcon* tray_icon_ = nullptr;
    QMenu* tray_menu_ = nullptr;
    QAction* show_hide_action_ = nullptr;
    QAction* quit_action_ = nullptr;
};

#endif
