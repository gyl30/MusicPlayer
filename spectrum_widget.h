#ifndef SPECTRUM_WIDGET_H
#define SPECTRUM_WIDGET_H

#include <QWidget>
#include <vector>

class spectrum_widget : public QWidget
{
    Q_OBJECT

   public:
    explicit spectrum_widget(QWidget* parent = nullptr);

   public slots:
    void update_spectrum(const std::vector<double>& magnitudes);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    std::vector<double> magnitudes_;
};

#endif
