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
int main()
{
	{
		ThreadPool tp;

		tp.setMode(PoolMode::CACHED);
		tp.start(1);
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());

		Result res = tp.submitTask(std::make_shared<MyTask>(1, 100));
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());
		tp.submitTask(std::make_shared<MyTask>());

	}
	system("pause");
	//std::this_thread::sleep_for(std::chrono::seconds(10));
	return 0;
}