#include "pool.h"
#include <iostream>

Task::Task() = default;
Task::~Task() = default;

ThreadPool::ThreadPool(int num_threads) : stopping(false) {
    // Create worker threads
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    // Cleanup any remaining task info structures
    for (auto &pair : task_map) {
        // If task was never waited for, delete it
        if (pair.second->task) {
            delete pair.second->task;
        }
        delete pair.second;
    }
}

void ThreadPool::SubmitTask(const std::string &name, Task *task) {
    std::unique_lock<std::mutex> lock(mtx);
    
    // Check if stopping - if so, reject the task
    if (stopping) {
        std::cout << "Cannot added task to queue" << std::endl;
        return;
    }
    
    // Create task info and add to queue
    TaskInfo *info = new TaskInfo(task, name);
    task_queue.push(info);
    task_map[name] = info;
    
    std::cout << "Added task: " << name << std::endl;
    
    // Notify one worker thread that work is available
    work_cv.notify_one();
}

void ThreadPool::WaitForTask(const std::string &name) {
    std::unique_lock<std::mutex> lock(mtx);
    
    // Find the task
    auto it = task_map.find(name);
    if (it == task_map.end()) {
        // Task not found - this shouldn't happen per assignment assumptions
        std::cerr << "Error: Task '" << name << "' not found" << std::endl;
        return;
    }
    
    TaskInfo *info = it->second;
    
    // Wait until the task is completed
    while (!info->completed) {
        info->completion_cv.wait(lock);
    }
    
    // Task is done, clean up
    delete info->task;  // Delete the task object
    delete info;        // Delete the task info
    task_map.erase(it); // Remove from map
}

void ThreadPool::worker_thread() {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Wait for work or stop signal
        work_cv.wait(lock, [this]() {
            return !task_queue.empty() || stopping;
        });
        
        // If stopping and no more work, exit
        if (stopping && task_queue.empty()) {
            std::cout << "Stopping thread" << std::endl;
            break;
        }
        
        // If there's no work but not stopping, continue waiting
        if (task_queue.empty()) {
            continue;
        }
        
        // Get the next task
        TaskInfo *info = task_queue.front();
        task_queue.pop();
        
        // Release lock before running task (task may take a long time)
        lock.unlock();
        
        // Run the task
        std::cout << "Started task: " << info->name << std::endl;
        info->task->Run();
        std::cout << "Finished task: " << info->name << std::endl;
        
        // Mark as completed and notify waiters
        lock.lock();
        info->completed = true;
        info->completion_cv.notify_all();
        lock.unlock();
    }
}

void ThreadPool::Stop() {
    std::cout << "Called Stop()" << std::endl;
    
    {
        std::unique_lock<std::mutex> lock(mtx);
        stopping = true;
        // Wake up all worker threads so they can check stopping flag
        work_cv.notify_all();
    }
    
    // Wait for all threads to complete
    for (auto &thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // Clean up any tasks that were never waited for
    std::unique_lock<std::mutex> lock(mtx);
    for (auto &pair : task_map) {
        TaskInfo *info = pair.second;
        // Delete task if it was completed but never waited for
        if (info->completed && info->task) {
            delete info->task;
            info->task = nullptr;
        }
        // Delete the task info
        delete info;
    }
    task_map.clear();
}