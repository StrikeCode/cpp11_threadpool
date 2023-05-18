#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

// 可以接受任意数据类型的类型
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	// 左值拷贝构造和赋值应该删除，因为unique_ptr不支持
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// 要点2：这个类型有一个能接受任意类型参数的构造函数，存在内部的派生类中
	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

	// 要点3：提取出Any对象的具体数据
	template<typename T>
	T cast_()
	{
		// 基类转派生类指针 RTTI转换
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is incompatible!";
		}
		return pd->data_;
	}
private:
	class Base
	{
	public:
		virtual ~Base() = default;
	};
	// 要点1
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data) {}
		T data_;
	};

private:
	std::unique_ptr<Base> base_;
};

class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}
	// 获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; }); // 阻塞在这里
		resLimit_--;

	}
	// 增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:

	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;

// 提交到线程池的task任务执行完成后的返回值类型
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//获取任务执行后的返回值
	void setVal(Any any);

	// get方法，用户调用这个方法获取task的返回值
	Any get();
private:
	Any any_; // 存储任务的返回值
	Semaphore sem_; // 线程通信
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; // 检查返回值是否有效
};

// 任务抽象基类
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	// 用户可自定义任务类型，从Task派生出来，重写run方法，实现自定义任务处理
	virtual Any run() = 0;
	
private:
	// 不要用智能指针，可能会导致和Result产生循环引用的问题
	Result* result_; // Result对象生命周期 > Task对象生命周期
}; 

enum class PoolMode // C++11
{
	MODE_FIXED,		// 固定数量的线程池
	MODE_CACHED,	// 线程池数量可变的线程池
};

class Thread
{
public:
	using ThreadFunc = std::function<void()>;
	
	Thread(ThreadFunc func);
	~Thread();

	// 启动线程
	void start();
private:
	ThreadFunc func_;
};

class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// 设置线程池工作模式
	void setMode(PoolMode mode);

	// 设置task任务队列上限阈值
	void settaskQueMaxThreadhold(int threshhold);

	// 给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	// 开启线程池
	void start(int initThreadSize = 4);

	// 禁止拷贝构造和赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数
	void threadFunc();

private:
	std::vector<std::unique_ptr<Thread>> threads_; // 线程列表
	size_t initThreadSize_; //初始线程数量

	// 任务队列
	std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列
	std::atomic_int taskSize_; // 任务的数量 ？
	int taskQueMaxThreadhold_; // 任务队列数量上限阈值
	
	std::mutex taskQueMtx_; // 保证任务队列的线程安全
	std::condition_variable notFull_; // 表示任务队列不满
	std::condition_variable notEmpty_; // 表示任务队列不空

	PoolMode poolMode_; // 线程池模式
};
#endif

