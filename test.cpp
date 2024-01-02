#include"threadpool.h"
#include<iostream>
#include <chrono>
using ull = unsigned long long;

int sum(int a1,int a2,int a3,int a4){
	return a1+a2+a3+a4;
}
int main()
{
	{
		ThreadPool tp;
		tp.start(4);

		std::future<int> res=tp.submitTask(sum,1,2,3,4);
		std::cout<<"res "<<res.get()<<std::endl;

		std::cout<<"res "<<tp.submitTask([](int begin,int end)->unsigned {
			unsigned rs=0;
			for(int i=begin;i<=end;i++)
			rs+=i;
			return rs;
		},1,100).get()<<std::endl;
		//std::cout<<res3.get().getData<int>()<<std::endl;
		getchar();
	}
	std::cout<<"finish"<<std::endl;
	
	//std::this_thread::sleep_for(std::chrono::seconds(10));
	return 0;
}
