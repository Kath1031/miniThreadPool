#include"threadpool.h"
#include<thread>
#include<iostream>

const int TASK_MAX_THRESHHOLD = INT16_MAX;
const int THREAD_MAX_THRESHHOLD = 10;

//当一个线程空闲THREAD_IDLE_TIME秒，将会被销毁
const int THREAD_IDLE_TIME = 100;

/*   ------------------------------Result---------------------------------------------- */
Result::Result(std::shared_ptr<Task>task, bool isVaild)
	:task_(task)
	, isVaild_(isVaild)
{
	task_->setResult(this);
}

Any Result::get()
{
	if (!isVaild_)
		return "";
	sem_.wait();
	return std::move(any_);
}
void Result::setVal(Any any)
{
	this->any_ = std::move(any);
	sem_.post();
}
/*   ------------------------------Task---------------------------------------------- */
Task::Task()
	:result_(nullptr)
{}
void Task::exec()
{
	if(result_!=nullptr)
	result_->setVal((work()));
}
void Task::setResult(Result* res)
{
	result_ = res;
}
/*   ------------------------------ThreadPool---------------------------------------------- */
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThreshold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreashold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::FIXED)
	, isRuning_(false)
	, idleThreadSize_(0)
	, curThreadSize_(0)
{}

ThreadPool::~ThreadPool()
{
	isRuning_ = false;
	std::cerr << "~ThreadPool" << std::endl;
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {
		return threads_.empty(); });
}

bool ThreadPool::checkRuningState() const
{
	return isRuning_;
}
//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode) {
	if (checkRuningState())
		return;
	this->poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxTreshold(int threshold) {
	if (checkRuningState())
		return;
	this->taskQueMaxThreshold_ = threshold;
}
void ThreadPool::setThreadSizeMaxTreshold(int threshold) {
	if (checkRuningState())
		return;
	if (poolMode_ == PoolMode::CACHED) {
		this->threadSizeThreashold_ = threshold;
	}
}
int ThreadPool::createThread() {
	auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
	int id = ptr->getID();
	std::cerr << "craete-->" << id << std::endl;
	this->threads_.emplace(id,std::move(ptr));
	++curThreadSize_;
	++idleThreadSize_;
	return id;
}
void ThreadPool::destoryThread(int threadid) {
	std::cerr << "destory-->" << threadid << std::endl;
	threads_.erase(threadid);
	--curThreadSize_;
	--idleThreadSize_;
}

//用户给线程池提交任务
//生产者消费者模型，用户生产task，线程池消费task
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	
	//获取锁
	std::unique_lock<std::mutex> lock(this->taskQueMtx_);

	//如果任务队列已满，等待任务队列有空余，线程通信
	//用户提交任务，最长阻塞不能超过1s，否则判断提交任务失败，返回
	if (!this->notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return (int)this->taskQue_.size() < (size_t)taskQueMaxThreshold_; }))
	{
		//等待1s，任务队列仍然是满的，提交任务失败
		std::cerr << "task queue is full,submit task fail." << std::endl;
		return Result(sp,false);
	}

	//如果任务队列有空余，把任务放入任务队列中
	this->taskQue_.emplace(sp);
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

	return Result(sp);
}

//线程函数 线程池的所有线程从任务队列里消费任务
void ThreadPool::threadFunc(int threadid) {
	//std::cout << "begin threadFunc tid "<< std::this_thread::get_id() << std::endl;
	//std::cout << "end threadfunc tid"<< std::this_thread::get_id() << std::endl;

	auto lastTime = std::chrono::high_resolution_clock().now();
	for(;;)
	{

		std::shared_ptr<Task> task;


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
			task->exec();
		}
		else {
			std::cerr << "task is null" << std::endl;
		}
		++idleThreadSize_;
		lastTime = std::chrono::high_resolution_clock().now();
	}

}
//开启线程池
void ThreadPool::start(int initThreadSize) {

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


/*   ------------------------------Thread---------------------------------------------- */
int Thread::generatedID_ = 0;
Thread::Thread(ThreadFunc func) 
	:func_(func)
	, threadID_(Thread::generatedID_++)
{}

Thread::~Thread() {

}
int Thread::getID() const {
	return threadID_;
}
void Thread:: start() {
	std::thread t(func_,threadID_);
	t.detach();
}