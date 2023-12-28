#include"threadpool.h"
#include<iostream>
#include <chrono>
using ull = unsigned long long;
class MyTask : public Task {
public:
	MyTask(int begin=1, int end=100000000) :begin_(begin), end_(end) {

	}
	Any work() {
		std::cout << "begin threadFunc tid " << std::this_thread::get_id() << std::endl;
		int sum = 0;
		for (int i = this->begin_; i <= this->end_; i++)
			sum += i;
		std::cout << "sum is" << sum << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(5));
		std::cout << "end threadfunc tid" << std::this_thread::get_id() << std::endl;
		return sum;
	}
private:
	int begin_, end_;
};

//当Result结束生命周期时，理应施放全部资源，但是g++对于condition_variable的析构函数是默认实现，因此出现死锁问题
int main()
{
	{
		ThreadPool tp;

		tp.setMode(PoolMode::CACHED);
		tp.start(2);
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());

		//std::cout<<res3.get().getData<int>()<<std::endl;
	}
	std::cout<<"finish"<<std::endl;
	getchar();
	//std::this_thread::sleep_for(std::chrono::seconds(10));
	return 0;
}