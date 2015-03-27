#ifndef DIRECTORY_CHANGE_FACTOR_H
#define DIRECTORY_CHANGE_FACTOR_H

#include "ReadDirectoryChanges.h"
#include<map>
using namespace std;

#define MSG_WAIT_INTERVAL 500*2

//��������Ŀ¼changes���࣬��һ������
//�����ʵ���и�worker�߳����������еĸı䣬���һص�ÿ���������Ļص�
class DirectoryChangeHandler
{
	friend class DirectoryMonitor;
public:
	//�и�worker�߳����������е�������������monitor�̵߳ĸ���
	DirectoryChangeHandler(int typeNum, int threadMax=3, DWORD waittime=MSG_WAIT_INTERVAL);
	~DirectoryChangeHandler();
	BOOL Terminate();

private:
	void Init();
	BOOL AddDirectory(DirectoryMonitor * monitor);
	BOOL DelDirectory(DirectoryMonitor * monitor);

	//worker �̴߳�����
	void handle_timeout();
	void handle_notify(int index);

	DWORD NextWaitTime(){
		return m_waittime;
	}
	
	static unsigned int WINAPI WorkThreadProc(LPVOID arg);
	//APC ����
	static void CALLBACK AddDirectoryProc(__in  ULONG_PTR arg);
	static void CALLBACK DelDirectoryProc(__in  ULONG_PTR arg);
	static void CALLBACK TerminateProc(__in  ULONG_PTR arg);
private:
	static int _id;
	int m_ntypes;	//�����������̨��ص������ģ�����MAXIMUM_WAIT_OBJECTS
	int m_threads;
	bool m_running;

	HANDLE m_hThread;
	unsigned int m_dwThreadId;
	DWORD m_waittime;	//�ȴ�����ʱ��
	DWORD m_def_waittime;	//Ĭ�ϵȴ�ʱ��
	map<int, DirectoryMonitor*>			m_monitors;		//�ص���Ҫ���
	map<string, CReadDirectoryChanges*> m_changes;		//type => changes

	//ͬ������
	unsigned int m_nHandles;
	HANDLE					m_changeHandles[MAXIMUM_WAIT_OBJECTS];
	CReadDirectoryChanges * m_changeArr[MAXIMUM_WAIT_OBJECTS];
	HANDLE					m_event;
};
#endif
