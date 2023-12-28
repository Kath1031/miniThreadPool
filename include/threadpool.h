
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
//threadpool status
enum class PoolMode {
    FIXED, //fixed thread number
    CACHED, //Dynamically adjusting the number of threads
};
class Semaphore {
public:
    Semaphore(int limit = 0)
        :resLimit_(limit)
        ,isExit_(false)
    {};
    ~Semaphore() {
        //isExit_=true;
        std::cout<<"释放资源"<<std::this_thread::get_id()<<std::endl;
    }
public:

    //获取一个资源的信号量
    void wait()
    {
            if(isExit_)
            return ;
        std::unique_lock<std::mutex> lock(mtx_);
        //阻塞，直到有信号量资源
                std::cout<<"-------wait"<<std::this_thread::get_id()<<std::endl;
        cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
        --resLimit_;
    }
    //增加一个信号量
    void post()
    {
        if(isExit_)
            return ;
        std::unique_lock<std::mutex> lock(mtx_);
        std::cout<<"---------post"<<std::this_thread::get_id()<<std::endl;
        ++resLimit_;
        cond_.notify_all();
    }
private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
    std::atomic_bool isExit_;
};

class Any {

public:
    Any() = default;
    ~Any() = default;

    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;

    Any(Any&& any) = default;
    Any& operator=(Any&&) = default;


    template<typename T>
    Any(T data) :basePtr_(std::make_unique<Derive<T>>(data)) {}

    template<typename T>
    T getData() {
        Derive<T>* ptr = dynamic_cast<Derive<T>*>(basePtr_.get());

        if (ptr != nullptr)
            return ptr->data_;
        else {
            throw "type error";
        }
    }
private:
    class Base {
    public:
        virtual ~Base() = default;
    };
    template<typename T>
    class Derive :public Base {
    public:
        Derive(T data) :data_(data) {}
        T data_;
    };

private:
    std::unique_ptr<Base> basePtr_;
};
class Task;

class Result {
public:
    Result(std::shared_ptr<Task>task, bool isVaild = true);
    ~Result() = default;
    Any get();
    void setVal(Any any);
private:
    Any any_;
    Semaphore sem_;
    std::shared_ptr<Task> task_;
    std::atomic_bool isVaild_;

};


class Task {
public:
    Task();
    ~Task() = default;
    virtual Any work() = 0;
    void setResult(Result *res);
    void exec();
private:
    Result* result_;//Result 的生命周期长于Task
};

//thread
class Thread {
public:
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func);
    ~Thread();
    void start();
    int getID() const;
private:
    ThreadFunc func_;
    int threadID_;
    static int generatedID_;
};


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
//threadpool
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();


    //开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());

    //设置线程池的工作模式
    void setMode(PoolMode mode);

    void setTaskQueMaxTreshold(int threshold);
    void setThreadSizeMaxTreshold(int threshold);

    //用户给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    //禁止
    ThreadPool(const ThreadPool&) = delete;
    Thread& operator= (const ThreadPool&) = delete;

private:
    //定义线程函数
    void threadFunc(int id);

    //检查线程池运行状态
    bool checkRuningState() const;

    //创造一个线程，并反回线程id
    int createThread();
    //销毁一个线程
    void destoryThread(int threadid);
private:
    std::unordered_map<int,std::unique_ptr<Thread>>threads_;
    int initThreadSize_;//初始线程数
    std::atomic_int curThreadSize_;//当前线程数
    int threadSizeThreashold_;//线程数上线
    std::atomic_int idleThreadSize_;//空闲线程数

    std::queue<std::shared_ptr<Task>> taskQue_;//task queue
    std::atomic_int taskSize_; //任务数量
    int taskQueMaxThreshold_;//任务队列数量上限阈值

    std::mutex taskQueMtx_; //保证任务队列线程安全

    std::condition_variable notFull_;//表示任务队列不满
    std::condition_variable notEmpty_;//表示任务队列不空
    std::condition_variable exitCond_;//等待线程资源全部回收


    PoolMode poolMode_;//线程池模式
    std::atomic_bool isRuning_;//线程池是否正在运行
};

#endif