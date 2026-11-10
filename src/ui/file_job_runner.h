#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <stop_token>

#include "core/work_queue.h"
#include "ui/file_operations.h"

namespace iv {

// Runs file jobs one at a time on the work queue, and tells how far the one running has got.
class FileJobRunner : public QObject {
    Q_OBJECT

public:
    using Work = std::function<FileJobResult(std::stop_token, const JobProgress&)>;
    using Finished = std::function<void(const FileJobResult&)>;

    explicit FileJobRunner(QObject* parent = nullptr);
    ~FileJobRunner() override;  // stops the job and waits for it

    bool busy() const { return m_busy; }
    QString name() const { return m_name; }
    int done() const { return m_done; }
    int total() const { return m_total; }

    // Starts the work unless a job is running. finished runs on this object's thread when the
    // work ends, once busy is false again.
    void start(const QString& name, int total, Work work, Finished finished);
    void cancel();  // stops before the next file

signals:
    void changed();

private:
    WorkQueue& m_queue = WorkQueue::global();
    WorkQueue::GroupId m_group = m_queue.createGroup();
    bool m_busy = false;
    QString m_name;
    int m_done = 0;
    int m_total = 0;
    std::stop_source m_stop;
};

}  // namespace iv
