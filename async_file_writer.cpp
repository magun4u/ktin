#include "async_file_writer.h"
#include "utils.h"
#include "win_util.h"

namespace
{
    constexpr size_t kMaxQueuedBytes = 4 * 1024 * 1024;
}

AsyncFileWriter::~AsyncFileWriter()
{
    Close();
}

bool AsyncFileWriter::Open(const std::wstring& path, bool append, bool writeUtf8BomIfNew)
{
    Close();

    DWORD creation = append ? OPEN_ALWAYS : CREATE_ALWAYS;
    DWORD access = append ? FILE_APPEND_DATA : GENERIC_WRITE;
    UniqueHandle file(CreateFileW(path.c_str(), access, FILE_SHARE_READ, nullptr,
        creation, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid())
        return false;

    if (append)
    {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(file.Get(), zero, nullptr, FILE_END))
            return false;
    }

    if (writeUtf8BomIfNew && GetFileSize(file.Get(), nullptr) == 0)
    {
        static const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        if (!WriteAllToWinFile(file.Get(), bom, sizeof(bom)))
            return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_file = file.Release();
        m_stopping = false;
        m_open = true;
        m_writing = false;
    }

    try
    {
        m_worker = std::thread(&AsyncFileWriter::WorkerLoop, this);
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ResetWinHandleRef(m_file);
        m_queue.clear();
        m_queuedBytes = 0;
        m_stopping = false;
        m_open = false;
        m_writing = false;
        return false;
    }

    return true;
}

void AsyncFileWriter::Write(const char* data, size_t len)
{
    if (!data || len == 0)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_open || m_file == INVALID_HANDLE_VALUE)
        return;

    if (len > kMaxQueuedBytes)
    {
        data += len - kMaxQueuedBytes;
        len = kMaxQueuedBytes;
    }

    while (!m_queue.empty() && m_queuedBytes + len > kMaxQueuedBytes)
    {
        m_queuedBytes -= m_queue.front().size();
        m_queue.pop_front();
    }

    m_queue.emplace_back(data, data + len);
    m_queuedBytes += len;
    m_cv.notify_one();
}

void AsyncFileWriter::Write(const std::string& data)
{
    Write(data.data(), data.size());
}

void AsyncFileWriter::Flush()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_open)
        return;
    m_flushCv.wait(lock, [&]() { return m_queue.empty() && !m_writing; });
    if (m_file != INVALID_HANDLE_VALUE)
        FlushFileBuffers(m_file);
}

void AsyncFileWriter::Close()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_open && m_file == INVALID_HANDLE_VALUE)
            return;
        m_stopping = true;
        m_cv.notify_one();
    }

    if (m_worker.joinable())
        m_worker.join();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(m_file);
        ResetWinHandleRef(m_file);
    }
    m_queue.clear();
    m_queuedBytes = 0;
    m_open = false;
    m_stopping = false;
    m_writing = false;
    m_flushCv.notify_all();
}

bool AsyncFileWriter::IsOpen() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_open && m_file != INVALID_HANDLE_VALUE;
}

void AsyncFileWriter::WorkerLoop()
{
    for (;;)
    {
        std::string chunk;
        HANDLE file = INVALID_HANDLE_VALUE;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&]() { return m_stopping || !m_queue.empty(); });
            if (m_queue.empty())
            {
                if (m_stopping)
                    break;
                continue;
            }
            chunk = std::move(m_queue.front());
            m_queuedBytes -= chunk.size();
            m_queue.pop_front();
            file = m_file;
            m_writing = true;
        }

        if (file != INVALID_HANDLE_VALUE && !chunk.empty())
            WriteAllToWinFile(file, chunk.data(), chunk.size());

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_writing = false;
            if (m_queue.empty())
                m_flushCv.notify_all();
        }
    }

    m_flushCv.notify_all();
}
