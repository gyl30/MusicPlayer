#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QKeyEvent>
#include "quick_editor.h"

quick_editor::quick_editor(const QString& initial_text, QWidget* parent) : QWidget(parent)
{
    setWindowFlags(Qt::Popup);
    setAttribute(Qt::WA_DeleteOnClose);

    line_edit_ = new QLineEdit(initial_text, this);
    confirm_button_ = new QPushButton("确认", this);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(line_edit_);
    layout->addWidget(confirm_button_);
    setLayout(layout);

    connect(confirm_button_, &QPushButton::clicked, this, &quick_editor::on_confirm_clicked);

    line_edit_->selectAll();
    line_edit_->setFocus();
}

void quick_editor::on_confirm_clicked()
{
    emit editing_finished(true, line_edit_->text());
    close();
}

void quick_editor::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        emit editing_finished(false, "");
        close();
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        on_confirm_clicked();
    }
    else
    {
        QWidget::keyPressEvent(event);
    }
}
