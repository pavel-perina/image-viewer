#include "ui/browse_context.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "core/image_source.h"
#include "core/work_queue.h"

namespace iv {

namespace {

constexpr int kScanPriority = 100;

QStringList imageNameFilters() {
    QStringList filters;
    for (const std::string_view extension : supportedExtensions()) {
        filters << QStringLiteral("*.") + QString::fromLatin1(extension);
    }
    return filters;
}

QStringList scanDirectory(const QString& directory) {
    QStringList names = QDir(directory).entryList(imageNameFilters(), QDir::Files | QDir::NoDotAndDotDot);
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(names, collator);
    return names;
}

}  // namespace

struct BrowseContext::Impl {
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    std::uint64_t scanGeneration = 0;
    QString directory;
    QString currentName;
    QStringList files;
    int currentIndex = -1;
};

BrowseContext::BrowseContext(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {}

BrowseContext::~BrowseContext() {
    // A running scan posts its result to this object, so it must finish first.
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

QString BrowseContext::directory() const { return m_impl->directory; }

QString BrowseContext::currentPath() const {
    return m_impl->currentName.isEmpty() ? QString() : QDir(m_impl->directory).filePath(m_impl->currentName);
}

QString BrowseContext::currentName() const { return m_impl->currentName; }
int BrowseContext::currentIndex() const { return m_impl->currentIndex; }
int BrowseContext::fileCount() const { return static_cast<int>(m_impl->files.size()); }
bool BrowseContext::hasNext() const { return m_impl->currentIndex + 1 < m_impl->files.size(); }
bool BrowseContext::hasPrevious() const { return m_impl->currentIndex > 0; }

void BrowseContext::openPath(const QString& path) {
    const QFileInfo info(path);
    const bool isDirectory = info.isDir();
    const QString directory = isDirectory ? info.absoluteFilePath() : info.absolutePath();

    m_impl->queue.cancelGroup(m_impl->group);
    const std::uint64_t generation = ++m_impl->scanGeneration;
    m_impl->directory = directory;
    m_impl->files.clear();
    m_impl->currentIndex = -1;
    // A file shows immediately; the rest of the directory arrives when the scan finishes.
    m_impl->currentName = isDirectory ? QString() : info.fileName();
    emit directoryChanged();
    emit filesChanged();
    emit currentChanged();

    m_impl->queue.submit(m_impl->group, kScanPriority, [this, directory, generation](std::stop_token) {
        QStringList files = scanDirectory(directory);
        QMetaObject::invokeMethod(this, [this, generation, files = std::move(files)] {
            if (generation != m_impl->scanGeneration) {
                return;
            }
            m_impl->files = files;
            if (m_impl->currentName.isEmpty() && !files.isEmpty()) {
                m_impl->currentName = files.front();
            }
            m_impl->currentIndex = static_cast<int>(files.indexOf(m_impl->currentName));
            emit filesChanged();
            emit currentChanged();
        });
    });
}

void BrowseContext::openUrl(const QUrl& url) { openPath(url.toLocalFile()); }

void BrowseContext::next() {
    if (hasNext()) {
        m_impl->currentName = m_impl->files.at(++m_impl->currentIndex);
        emit currentChanged();
    }
}

void BrowseContext::previous() {
    if (hasPrevious()) {
        m_impl->currentName = m_impl->files.at(--m_impl->currentIndex);
        emit currentChanged();
    }
}

}  // namespace iv
