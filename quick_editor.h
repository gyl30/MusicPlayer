#ifndef QUICK_EDITOR_H
#define QUICK_EDITOR_H

#include <QWidget>

class QLineEdit;
class QPushButton;
class QKeyEvent;

class quick_editor : public QWidget
{
    Q_OBJECT

   public:
    explicit quick_editor(const QString& initial_text, QWidget* parent = nullptr);

   signals:
    void editing_finished(bool accepted, const QString& text);

   protected:
    void keyPressEvent(QKeyEvent* event) override;

   private slots:
    void on_confirm_clicked();

   private:
    QLineEdit* line_edit_ = nullptr;
    QPushButton* confirm_button_ = nullptr;
};

#endif
