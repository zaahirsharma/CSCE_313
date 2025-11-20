#ifndef POOL_H
#define POOL_H

#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

class Task {
public:
    Task();
    virtual ~Task();
    virtual void Run() = 0;  // implemented by subclass
};

class ThreadPool {
public:
    explicit ThreadPool(int num_threads);
    ~ThreadPool();

    // Submit a task with a particular name.
    void SubmitTask(const std::string &name, Task *task);
    
    // Wait for a task to complete
    void WaitForTask(const std::string &name);

    // Stop all threads
    void Stop();

private:
    struct TaskInfo {
        Task *task;
        std::string name;
        bool completed;
        std::condition_variable completion_cv;
        
        TaskInfo(Task *t, const std::string &n) 
            : task(t), name(n), completed(false) {}
    };

    void worker_thread();

    std::mutex mtx;
    std::condition_variable work_cv;  // Notify workers of new work
    
    std::vector<std::thread> threads;
    std::queue<TaskInfo*> task_queue;
    std::unordered_map<std::string, TaskInfo*> task_map;  // Track tasks by name
    
    bool stopping;
};

#endif // POOL_H