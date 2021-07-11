#pragma once
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>


class ThreadPool
{
public:
    //构造函数只是启动了一些worker
    ThreadPool(size_t);
    //析构函数join所有线程
    ~ThreadPool();
    //向池中添加新的worker
    template<typename F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
private:
    //需要跟踪线程，以便我们可以添加删除它们
    std::vector<std::thread> workers;
    //任务队列
    std::queue<std::function<void()>> tasks;
    //任务队列互斥锁
    std::mutex tasks_mutex;
    //同步信号
    std::condition_variable condition;
    bool stop;
};

inline ThreadPool::ThreadPool(size_t threads) :
    stop(false)
{
    for(size_t i = 0; i < threads; i++)
    {
        workers.emplace_back(
                    [this]
            {
                for(;;)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->tasks_mutex);
                        this->condition.wait(lock,
                                             [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty())
                        {
                            return;
                        }
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            }
        );
    }
}

inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(tasks_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread & worker : workers)
    {
        worker.join();
    }
}

template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    //std::forward参数完美转发: https://zhuanlan.zhihu.com/p/92486757
    auto task = std::make_shared<std::packaged_task<return_type()>>(
                            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(tasks_mutex);
        //停止池后不允许排队
        if(stop)
        {
            throw std::runtime_error("enqueue on stopped ThreadPool!");
        }
        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

#endif // THREADPOOL_H
