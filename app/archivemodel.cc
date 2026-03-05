#include "archivemodel.h"

#include <QDateTime>
#include <QFileInfo>
#include <QBrush>
#include <QFont>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ArchiveModel::ArchiveModel(QObject* parent)
    : QStandardItemModel(parent)
{
    setupHeaders();
}

void ArchiveModel::setupHeaders()
{
    setColumnCount(static_cast<int>(Column::COUNT));
    setHorizontalHeaderLabels({
        tr("Name"),
        tr("Size"),
        tr("Compressed"),
        tr("Ratio"),
        tr("Type"),
        tr("Modified"),
        tr("Created"),
        tr("SHA-256"),
        tr("Encrypted"),
    });
}

// ---------------------------------------------------------------------------
// Load from reader
// ---------------------------------------------------------------------------

void ArchiveModel::loadFromArchive(xstd::Archive& archive)
{
    clear();

    auto files = archive.ListFiles();

    for (const auto& filePath : files) {
        auto meta = archive.Stat(filePath);
        if (!meta.has_value()) continue;

        const auto& m = meta.value();

        // Compute compressed size from pages
        int64_t compressedSize = 0;
        bool encrypted = false;
        for (const auto& page : m.pages) {
            compressedSize += page.compressed_size;
            if (page.IsEncrypted()) encrypted = true;
        }

        double ratio = (m.original_size > 0)
            ? (static_cast<double>(compressedSize) / static_cast<double>(m.original_size)) * 100.0
            : 0.0;

        QString name = QString::fromStdString(m.file_name);

        QList<QStandardItem*> row;
        row.reserve(static_cast<int>(Column::COUNT));

        // Name
        auto* nameItem = new QStandardItem(name);
        nameItem->setEditable(false);
        row.append(nameItem);

        // Size
        auto* sizeItem = new QStandardItem(humanSize(m.original_size));
        sizeItem->setData(m.original_size, Qt::UserRole);  // for sorting
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeItem->setEditable(false);
        row.append(sizeItem);

        // Compressed
        auto* compItem = new QStandardItem(humanSize(compressedSize));
        compItem->setData(compressedSize, Qt::UserRole);
        compItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        compItem->setEditable(false);
        row.append(compItem);

        // Ratio
        auto* ratioItem = new QStandardItem(QString("%1%").arg(ratio, 0, 'f', 1));
        ratioItem->setData(ratio, Qt::UserRole);
        ratioItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ratioItem->setEditable(false);
        row.append(ratioItem);

        // Type
        auto* typeItem = new QStandardItem(fileExtension(name));
        typeItem->setEditable(false);
        row.append(typeItem);

        // Modified
        auto modDt = QDateTime::fromSecsSinceEpoch(m.last_modified_time);
        auto* modItem = new QStandardItem(modDt.toString("yyyy-MM-dd HH:mm:ss"));
        modItem->setData(m.last_modified_time, Qt::UserRole);
        modItem->setEditable(false);
        row.append(modItem);

        // Created
        auto creDt = QDateTime::fromSecsSinceEpoch(m.created_time);
        auto* creItem = new QStandardItem(creDt.toString("yyyy-MM-dd HH:mm:ss"));
        creItem->setData(m.created_time, Qt::UserRole);
        creItem->setEditable(false);
        row.append(creItem);

        // Checksum (SHA-256, abbreviated to first 16 hex chars)
        auto* hashItem = new QStandardItem(checksumHex(m.checksum));
        hashItem->setEditable(false);
        hashItem->setFont(QFont("Consolas", 9));
        row.append(hashItem);

        // Encrypted
        auto* encItem = new QStandardItem(encrypted ? tr("Yes") : tr("No"));
        encItem->setTextAlignment(Qt::AlignCenter);
        encItem->setEditable(false);
        if (encrypted)
            encItem->setForeground(QBrush(QColor("#e67e22")));
        row.append(encItem);

        appendRow(row);
    }
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void ArchiveModel::clear()
{
    removeRows(0, rowCount());
    setupHeaders();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString ArchiveModel::humanSize(int64_t bytes)
{
    if (bytes < 0) return "?";

    constexpr int64_t KB = 1024;
    constexpr int64_t MB = 1024 * KB;
    constexpr int64_t GB = 1024 * MB;

    if (bytes >= GB)
        return QString("%1 GB").arg(static_cast<double>(bytes) / static_cast<double>(GB), 0, 'f', 2);
    if (bytes >= MB)
        return QString("%1 MB").arg(static_cast<double>(bytes) / static_cast<double>(MB), 0, 'f', 2);
    if (bytes >= KB)
        return QString("%1 KB").arg(static_cast<double>(bytes) / static_cast<double>(KB), 0, 'f', 1);
    return QString("%1 B").arg(bytes);
}

QString ArchiveModel::fileExtension(const QString& name)
{
    int dot = name.lastIndexOf('.');
    if (dot < 0 || dot == name.size() - 1) return tr("File");
    return name.mid(dot + 1).toUpper();
}

QString ArchiveModel::checksumHex(const std::array<uint8_t, 32>& hash)
{
    // Show first 8 bytes (16 hex chars) with ellipsis
    QString hex;
    hex.reserve(19);
    for (int i = 0; i < 8; ++i)
        hex += QString("%1").arg(hash[i], 2, 16, QChar('0'));
    hex += "...";
    return hex;
}
