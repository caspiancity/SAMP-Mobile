#include "CFPSFix.h"
#include "main.h"
#include <sys/syscall.h>
#include <algorithm>
#include <cerrno>
#include <chrono>

static long setThreadAffinityMask(pid_t tid, uint64_t mask)
{
    return syscall(__NR_sched_setaffinity, tid, sizeof(mask), &mask);
}

void CFPSFix::Routine()
{
    unsigned n = std::thread::hardware_concurrency();
    if (n == 0 || n > 64) n = 8;
    uint64_t mask = (n >= 64) ? ~0ULL : ((1ULL << n) - 1);

    std::unique_lock<std::mutex> lock(m_Mutex);
    while (!m_bStop)
    {
        for (auto it = m_Threads.begin(); it != m_Threads.end();)
        {
            if (setThreadAffinityMask(*it, mask) != 0 && errno == ESRCH)
                it = m_Threads.erase(it); // thread artiq yoxdur
            else
                ++it;
        }

        m_Cv.wait_for(lock, std::chrono::milliseconds(1000), [this] { return m_bStop; });
    }
}

CFPSFix::CFPSFix()
{
    m_Worker = std::thread(&CFPSFix::Routine, this);
}

CFPSFix::~CFPSFix()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_bStop = true;
    }
    m_Cv.notify_all();
    if (m_Worker.joinable()) m_Worker.join();
}

void CFPSFix::PushThread(pid_t tid)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (std::find(m_Threads.begin(), m_Threads.end(), tid) == m_Threads.end())
        m_Threads.push_back(tid);
}