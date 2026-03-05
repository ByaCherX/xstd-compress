#include "welcomewidget.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QSpacerItem>

WelcomeWidget::WelcomeWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    // Spacer top
    layout->addStretch(2);

    // Title
    auto* title = new QLabel("XSTD", this);
    QFont titleFont("Segoe UI", 42, QFont::Bold);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color: #2d89ef;");
    layout->addWidget(title);

    // Subtitle
    auto* subtitle = new QLabel(tr("Archive Manager"), this);
    QFont subFont("Segoe UI", 16);
    subtitle->setFont(subFont);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet("color: #666;");
    layout->addWidget(subtitle);

    layout->addSpacing(20);

    // Description
    auto* desc = new QLabel(
        tr("A modern archive format supporting ZSTD compression,\n"
           "AES-GCM/CTR encryption, SHA-256 integrity verification,\n"
           "delta encoding, and B+ Tree file catalog."),
        this);
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    QFont descFont("Segoe UI", 10);
    desc->setFont(descFont);
    desc->setStyleSheet("color: #888; line-height: 1.5;");
    layout->addWidget(desc);

    layout->addSpacing(30);

    // Buttons container
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setAlignment(Qt::AlignCenter);
    btnLayout->setSpacing(16);

    auto* openBtn = new QPushButton(tr("  Open XSTD Archive  "), this);
    openBtn->setMinimumHeight(40);
    openBtn->setFont(QFont("Segoe UI", 11));
    openBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #2d89ef;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 8px 24px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1b6ec2;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #145a9e;"
        "}"
    );
    connect(openBtn, &QPushButton::clicked, this, &WelcomeWidget::openArchiveRequested);
    btnLayout->addWidget(openBtn);

    auto* createBtn = new QPushButton(tr("  Create New Archive  "), this);
    createBtn->setMinimumHeight(40);
    createBtn->setFont(QFont("Segoe UI", 11));
    createBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #444;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 8px 24px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #333;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #222;"
        "}"
    );
    connect(createBtn, &QPushButton::clicked, this, &WelcomeWidget::createArchiveRequested);
    btnLayout->addWidget(createBtn);

    layout->addLayout(btnLayout);

    // Spacer bottom
    layout->addStretch(3);

    // Version info at bottom
    auto* version = new QLabel(tr("XSTD Format v2  |  App v1.0.0"), this);
    version->setAlignment(Qt::AlignCenter);
    version->setStyleSheet("color: #aaa; font-size: 9px;");
    layout->addWidget(version);

    layout->addSpacing(10);
}
