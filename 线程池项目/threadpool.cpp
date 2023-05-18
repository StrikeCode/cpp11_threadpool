#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHOLD = 4;
ThreadPool::ThreadPool()
	: initThreadSize_(4)
	, taskSize_(0)
	, taskQueMaxThreadhold_(TASK_MAX_THRESHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
{

}

ThreadPool::~ThreadPool()
{}

// �����̳߳ع���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}

// ����task�������������ֵ
void ThreadPool::settaskQueMaxThreadhold(int threshhold)
{
	taskQueMaxThreadhold_ = threshhold;
}

// ���̳߳��ύ����
// ����һ���Զ��еĲ��������ٽ���Դ
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��
	std::unique_lock<std::mutex> lck(taskQueMtx_);
	// �̵߳�ͨ��, �ȴ������п���
	//while (taskQue_.size() == taskQueMaxThreadhold_)
	//{
	//	notFull_.wait(lck); // �ȴ����в���
	//}

	// �ȼ��������while
	// ����ڶ����pred�����˳�while
	// ��1sû�������˳�����
	if (!notFull_.wait_for(lck, std::chrono::seconds(1), [&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreadhold_ ;}))
	{
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp, false);//
	}

	// ����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++; // ???

	// ������в��գ�����notEmpty_��wait,��֪ͨ����������
	notEmpty_.notify_all();

	// ���������Result����
	return Result(sp);
}

// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;

	// �����̶߳���
	// ���̺߳�������thread�̶߳���
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		// unique_ptr�Ŀ�������͸�ֵ����delete,����Ҫ��ptrתΪ��ֵ
		threads_.emplace_back(std::move(ptr));
	}

	// ���������߳�
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // ��Ҫȥִ��һ���̺߳���
	}
}

// �̳߳�������������������
void ThreadPool::threadFunc()
{
	/*std::cout << " begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	std::cout << " endthreadFunc tid:" << std::this_thread::get_id() << std::endl;*/
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// ��ȡ��
			std::unique_lock<std::mutex> lck(taskQueMtx_);
			std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;
			// �ȴ�notEmpty_����
			notEmpty_.wait(lck, [&]()->bool { return taskQue_.size() > 0; });

			std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�..." << std::endl;
			// �����������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// ����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ȡ��һ���������֪ͨ �����߿��Խ�����������
			notFull_.notify_all();
		} // ֻ��Ҫ��taskQue_������ٽ���Դ�����ż�����ִ�в���Ҫ����

		// ��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			//task->run(); 
			// ִ�����񣬰�����ķ���ֵsetVal��������Result
			task->exec();
		}
		
	}	
}

//////////////////////// class Thread implementation

Thread::Thread(ThreadFunc func)
	: func_(func)
{

}
Thread::~Thread()
{

}
// �����߳�
void Thread::start()
{
	// ����һ���߳���ִ��һ���̺߳���
	std::thread t(func_);
	t.detach(); //  ���÷����̣߳�t�߳��Լ������Լ���������
}
//////////////////////// class Task implementation
Task::Task()
	: result_(nullptr)
{}
void Task::exec()
{
	if (nullptr != result_)
	{
		result_->setVal(run());
	}
	
}

void Task::setResult(Result* res)
{
	result_ = res;
}

//////////////////////// class Result implementation
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{
	task->setResult(this);
}

//��ȡ����ִ�к�ķ���ֵ
void Result::setVal(Any any)
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);  // Any ��ֻ�ṩ��ֵ�������ƶ���ֵ����
	sem_.post(); // ��ȡ����ķ���ֵ�������ź�����Դ
}

// get�������û��������������ȡtask�ķ���ֵ
Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // task�������ûִ���꣬����������û��߳�
	return std::move(any_); // Any���ṩ��ֵ��������Ϳ�����ֵ
}