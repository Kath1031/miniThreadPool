
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <iostream>
#include <future>

const int TASK_MAX_THRESHHOLD = INT16_MAX;
const int THREAD_MAX_THRESHHOLD = 10;

//当一个线程空闲THREAD_IDLE_TIME秒，将会被销毁
const int THREAD_IDLE_TIME = 100;
// threadpool status
enum class PoolMode
{
    FIXED,  // fixed thread number
    CACHED, // Dynamically adjusting the number of threads
};

// thread
class Thread
{
public:
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func) 
        :func_(func)
        , threadID_(Thread::generatedID_++)
    {}

    ~Thread()=default;
    void start() {
        std::thread t(func_,threadID_);
        t.detach();
    }
    int getID() const {
        return threadID_;
    }

private:
    ThreadFunc func_;
    int threadID_;
    static int generatedID_;
};
int Thread::generatedID_=0;
/*
example:

    class MyTask:Task{
        public:
            void work(){
                your code
            };
    };
    ThreadPool tp;
    tp.start(4);
    tp.submitTask(std::make_shared<MyTask>());


*/


/*   ------------------------------ThreadPool---------------------------------------------- */
class ThreadPool
{
public:
    ThreadPool()
        : initThreadSize_(0)
        ,taskSize_(0)
        , taskQueMaxThreshold_(TASK_MAX_THRESHHOLD)
        , threadSizeThreashold_(THREAD_MAX_THRESHHOLD)
        , poolMode_(PoolMode::FIXED)
        , isRuning_(false)
        , idleThreadSize_(0)
        , curThreadSize_(0)
    {}

    ~ThreadPool()
    {
        isRuning_ = false;
        std::cerr << "~ThreadPool" << std::endl;
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        notEmpty_.notify_all();
        exitCond_.wait(lock, [&]() -> bool
                       { return threads_.empty(); });
    }

    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency()) {

        this->initThreadSize_ = initThreadSize;
        this->isRuning_ = true;
        
        //创建所有线程
        for (int i = 0; i < initThreadSize; i++)
        {
            createThread();
        }

        //启动所有线程 
        for (int i = 0; i < initThreadSize; i++)
        {
            this->threads_[i]->start();
        }

    }

    //设置线程池的工作模式
    void setMode(PoolMode mode) {
	    if (checkRuningState())
		    return;
	    this->poolMode_ = mode;
    }
    void setTaskQueMaxTreshold(int threshold) {
	    if (checkRuningState())
		    return;
	    this->taskQueMaxThreshold_ = threshold;
    }
    void setThreadSizeMaxTreshold(int threshold) {
	if (checkRuningState())
		return;
	if (poolMode_ == PoolMode::CACHED) {
		this->threadSizeThreashold_ = threshold;
	}
}

    //用户给线程池提交任务
    //生产者消费者模型，用户生产task，线程池消费task
    //submitTask(sum,a1,a2,a3...)
    template<typename Func,typename... Args>
    auto submitTask(Func &&func,Args&&... args)->std::future<decltype(func(args...))>
    {
        using RType=decltype(func(args...));
        auto task=std::make_shared<std::packaged_task<RType()>>(
            std::bind(std::forward<Func>(func),std::forward<Args>(args)...)
        );
        std::future<RType> result=task->get_future();

                //获取锁
        std::unique_lock<std::mutex> lock(this->taskQueMtx_);

        //如果任务队列已满，等待任务队列有空余，线程通信
        //用户提交任务，最长阻塞不能超过1s，否则判断提交任务失败，返回
        if (!this->notFull_.wait_for(lock, std::chrono::seconds(1),
            [&]()->bool {return (int)this->taskQue_.size() < (size_t)taskQueMaxThreshold_; }))
        {
            //等待1s，任务队列仍然是满的，提交任务失败
            std::cerr << "task queue is full,submit task fail." << std::endl;
            auto task=std::make_shared<std::packaged_task<RType()>>(
                []()->RType{return RType();}
            );
            (*task)();
            return task->get_future();
        }

        //如果任务队列有空余，把任务放入任务队列中
        this->taskQue_.emplace(
            [task]() {(*task)();}
        );
        this->taskSize_++;

        //因为提交一个任务，所以任务队列非空，在notEmpty_上进行通知
        notEmpty_.notify_all();

        //cached模式下，当线程数不够时，需要考虑是否开辟新线程
        if (poolMode_ == PoolMode::CACHED
            && taskSize_ > idleThreadSize_
            && taskSize_ < threadSizeThreashold_) 
        {
            int threadid=createThread();
            threads_[threadid]->start();
        }

        return result;
    }
    // 禁止
    ThreadPool(const ThreadPool &) = delete;
    Thread &operator=(const ThreadPool &) = delete;

private:
    // 定义线程函数
    //线程函数 线程池的所有线程从任务队列里消费任务
    void threadFunc(int threadid) {
        //std::cout << "begin threadFunc tid "<< std::this_thread::get_id() << std::endl;
        //std::cout << "end threadfunc tid"<< std::this_thread::get_id() << std::endl;

        auto lastTime = std::chrono::high_resolution_clock().now();
        for(;;)
        {

            Task task;
            {
                //获取锁
                std::unique_lock<std::mutex> lock(this->taskQueMtx_);

                std::cout << "尝试获取任务 tid " << std::this_thread::get_id() << std::endl;

                while (taskQue_.empty()) {

                    //线程池结束，跳出循环
                    if (!isRuning_) {
                        destoryThread(threadid);
                        exitCond_.notify_all();
                        return ;
                    }

                    //如果是cached模式，需要等待一定时间
                    if (poolMode_ == PoolMode::CACHED) {
                        if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                            auto nowTime = std::chrono::high_resolution_clock().now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);
                            if (dur.count() >= THREAD_IDLE_TIME
                                && curThreadSize_ > initThreadSize_) {
                                destoryThread(threadid);
                                return;
                            }
                        }
                    }
                    else {
                        this->notEmpty_.wait(lock);
                    }
                }


                //从任务队列中取一个任务出来
                --idleThreadSize_;
                std::cout << "获取任务成功 tid " << std::this_thread::get_id() << std::endl;
                task = taskQue_.front();
                this->taskQue_.pop();
                this->taskSize_--;

                //如果有剩余任务，继续通知其他线程执行任务
                if (this->taskQue_.size() > 0)
                {
                    this->notEmpty_.notify_all();
                }

                //通知可以继续提交生产任务
                this->notFull_.notify_all();
            }


            //当前线程负责执行这个任务
            if (task != nullptr) {
                task();
            }
            else {
                std::cerr << "task is null" << std::endl;
            }
            ++idleThreadSize_;
            lastTime = std::chrono::high_resolution_clock().now();
        }

    }

    // 检查线程池运行状态
    bool checkRuningState() const{
        return isRuning_;
    }

    // 创造一个线程，并反回线程id
    int createThread() {
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
        int id = ptr->getID();
        std::cerr << "craete-->" << id << std::endl;
        this->threads_.emplace(id,std::move(ptr));
        ++curThreadSize_;
        ++idleThreadSize_;
        return id;
    }
    // 销毁一个线程
    void destoryThread(int threadid) {
        std::cerr << "destory-->" << threadid << std::endl;
        threads_.erase(threadid);
        --curThreadSize_;
        --idleThreadSize_;
    }

private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;
    int initThreadSize_;             // 初始线程数
    std::atomic_int curThreadSize_;  // 当前线程数
    int threadSizeThreashold_;       // 线程数上线
    std::atomic_int idleThreadSize_; // 空闲线程数

    using Task=std::function<void()>;
    std::queue<Task> taskQue_; // task queue
    std::atomic_int taskSize_;                  // 任务数量
    int taskQueMaxThreshold_;                   // 任务队列数量上限阈值

    std::mutex taskQueMtx_; // 保证任务队列线程安全

    std::condition_variable notFull_;  // 表示任务队列不满
    std::condition_variable notEmpty_; // 表示任务队列不空
    std::condition_variable exitCond_; // 等待线程资源全部回收

    PoolMode poolMode_;         // 线程池模式
    std::atomic_bool isRuning_; // 线程池是否正在运行
};

#endif