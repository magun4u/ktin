#pragma once

#include <windows.h>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class AsyncFileWriter
{
public:
    AsyncFileWriter() = default;
    ~AsyncFileWriter();

    AsyncFileWriter(const AsyncFileWriter&) = delete;
    AsyncFileWriter& operator=(const AsyncFileWriter&) = delete;

    bool Open(const std::wstring& path, bool append, bool writeUtf8BomIfNew);
    void Write(const char* data, size_t len);
    void Write(const std::string& data);
    void Flush();
    void Close();
    bool IsOpen() const;

private:
    void WorkerLoop();

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_flushCv;
    std::deque<std::string> m_queue;
    size_t m_queuedBytes = 0;
    std::thread m_worker;
    HANDLE m_file = INVALID_HANDLE_VALUE;
    bool m_stopping = false;
    bool m_open = false;
    bool m_writing = false;
};
