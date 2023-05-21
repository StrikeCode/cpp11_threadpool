#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include <functional>

// ���Խ��������������͵�����
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	// ��ֵ��������͸�ֵӦ��ɾ������Ϊunique_ptr��֧��
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// Ҫ��2�����������һ���ܽ����������Ͳ����Ĺ��캯���������ڲ�����������
	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

	// Ҫ��3����ȡ��Any����ľ�������
	template<typename T>
	T cast_()
	{
		// ����ת������ָ�� RTTIת��
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
	// Ҫ��1
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
	~Semaphore() = default;
	// ��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; }); // ����������
		resLimit_--;

	}
	// ����һ���ź�����Դ
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

// �ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//��ȡ����ִ�к�ķ���ֵ
	void setVal(Any any);

	// get�������û��������������ȡtask�ķ���ֵ
	Any get();
private:
	Any any_; // �洢����ķ���ֵ
	Semaphore sem_; // �߳�ͨ��
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; // ��鷵��ֵ�Ƿ���Ч
};

// ����������
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	// �û����Զ����������ͣ���Task������������дrun������ʵ���Զ���������
	virtual Any run() = 0;
	
private:
	// ��Ҫ������ָ�룬���ܻᵼ�º�Result����ѭ�����õ�����
	Result* result_; // Result������������ > Task������������
}; 

enum class PoolMode // C++11
{
	MODE_FIXED,		// �̶��������̳߳�
	MODE_CACHED,	// �̳߳������ɱ���̳߳�
};

class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	
	Thread(ThreadFunc func);
	~Thread();

	// �����߳�
	void start();

	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_; 
	int threadId_; // �����̶߳����id,����ָϵͳ�����̺߳�
};

class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// �����̳߳ع���ģʽ
	void setMode(PoolMode mode);

	// ����task�������������ֵ
	void setTaskQueMaxThreadhold(int threshhold);

	// �����̳߳�cachedģʽ���̵߳�������ֵ
	void setThreadSizeThreadhold(int threshhold);

	// ���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());

	// ��ֹ��������͸�ֵ
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �����̺߳���
	void threadFunc(int threadid);

	// ���pool������״̬
	bool checkRunningState() const;

private:
	//std::vector<std::unique_ptr<Thread>> threads_; // �߳��б�
	// �����̵߳�id���̶߳���ָ���ӳ�䣬�����ѯ
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳��б�
	size_t initThreadSize_; //��ʼ�߳�����
	int threadSizeThreshHold_; // �߳��������ޣ�cachedģʽʹ�ã�
	std::atomic_int curThreadSize_;  // ��¼��ǰ�̳߳������̵߳���������cachedģʽʹ�ã�
	std::atomic_int idleThreadSize_; // ��¼�����̵߳�������cachedģʽʹ�ã�

	// �������
	std::queue<std::shared_ptr<Task>> taskQue_; // �������
	std::atomic_int taskSize_; // ��������� ��
	int taskQueMaxThreshHold_; // �����������������ֵ
	
	std::mutex taskQueMtx_; // ��֤������е��̰߳�ȫ
	std::condition_variable notFull_; // ��ʾ������в���
	std::condition_variable notEmpty_; // ��ʾ������в���
	std::condition_variable exitCond_; // �ȵ��߳���Դȫ������

	PoolMode poolMode_; // �̳߳�ģʽ
	std::atomic_bool isPoolRunning_; // �̳߳�����״̬
};
#endif

