#include "ui/file_job_runner.h"

#include <utility>

namespace iv {

namespace {

constexpr int kJobPriority = 100;

}  // namespace

FileJobRunner::FileJobRunner(QObject* parent) : QObject(parent) {}

FileJobRunner::~FileJobRunner() {
    // Running work posts its progress and result to this object, so it must finish first.
    m_stop.request_stop();
    m_queue.cancelGroup(m_group);
    m_queue.waitGroup(m_group);
}

void FileJobRunner::start(const QString& name, int total, Work work, Finished finished) {
    if (m_busy) {
        return;
    }
    m_busy = true;
    m_name = name;
    m_done = 0;
    m_total = total;
    m_stop = std::stop_source();
    emit changed();
    m_queue.submit(m_group, kJobPriority, [this, work = std::move(work), finished = std::move(finished), stop = m_stop.get_token()](std::stop_token) {
        const JobProgress progress = [this](int count) {
            QMetaObject::invokeMethod(this, [this, count] {
                m_done = count;
                emit changed();
            });
        };
        FileJobResult result = work(stop, progress);
        QMetaObject::invokeMethod(this, [this, finished, result = std::move(result)] {
            m_busy = false;
            emit changed();
            finished(result);
        });
    });
}

void FileJobRunner::cancel() { m_stop.request_stop(); }

}  // namespace iv
