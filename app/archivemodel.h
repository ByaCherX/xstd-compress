#pragma once

#include <QStandardItemModel>
#include <QString>
#include <vector>

#include "archive.h"
#include "metadata.h"

/// Column indices for the archive file model.
enum class Column : int {
    Name = 0,
    Size,
    Compressed,
    Ratio,
    Type,
    Modified,
    Created,
    Checksum,
    Encrypted,
    COUNT  // sentinel — total number of columns
};

/// A QStandardItemModel that displays files from an opened Archive.
class ArchiveModel : public QStandardItemModel {
    Q_OBJECT

public:
    explicit ArchiveModel(QObject* parent = nullptr);

    /// Populate the model from an open Archive.
    void loadFromArchive(xstd::Archive& archive);

    /// Clear all rows (but keep headers).
    void clear();

private:
    void setupHeaders();

    static QString humanSize(int64_t bytes);
    static QString fileExtension(const QString& name);
    static QString checksumHex(const std::array<uint8_t, 32>& hash);
};
