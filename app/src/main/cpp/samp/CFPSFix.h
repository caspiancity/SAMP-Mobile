#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/types.h>

class CFPSFix
{
private:
    void Routine();

    std::mutex m_Mutex;
    std::condition_variable m_Cv;
    std::vector<pid_t> m_Threads;
    std::thread m_Worker;
    bool m_bStop = false;
public:
    CFPSFix();
    ~CFPSFix();

    void PushThread(pid_t tid);
};