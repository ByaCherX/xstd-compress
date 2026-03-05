#pragma once

#include <QWidget>

class QPushButton;
class QLabel;

class WelcomeWidget : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget* parent = nullptr);

signals:
    void openArchiveRequested();
    void createArchiveRequested();
};
