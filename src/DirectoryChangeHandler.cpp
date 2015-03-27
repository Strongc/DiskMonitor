#include "DirectoryMonitor.h"
#include "DirectoryChangeHandler.h"

//Ϊ���ڳ�ʱʱ��ɾ��һЩ��Դ����ѡ��ܳ���ʱ��
#define MAX_SLEEP_TIME 30*60*1000

int DirectoryChangeHandler::_id = 0;

DirectoryChangeHandler::DirectoryChangeHandler(int typeNum, int threadMax, DWORD waittime)
	:m_ntypes(typeNum), m_threads(threadMax) 
{
	m_running = true;
	m_hThread = NULL;
	m_dwThreadId = 0;
	m_nHandles = 0;
	m_waittime = MAX_SLEEP_TIME - 1/*INFINITE*/;
	m_def_waittime = waittime;
	//log
	cout2file();
	//multi wait ����Ҫ�еȴ���handler
	m_event = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	m_changeHandles[0] = m_event;
	m_changeArr[0] = NULL;
	m_nHandles = 1;
}

DirectoryChangeHandler::~DirectoryChangeHandler()
{
	Terminate();
}

void DirectoryChangeHandler::Init()
{
	//worker thread,
	m_hThread = (HANDLE)_beginthreadex(NULL, 
		0, 
		DirectoryChangeHandler::WorkThreadProc,
		this, 
		0, 
		&m_dwThreadId);
}

//�첽APC ��֪ͨ����̣߳�����������������߳̽���ִ�еģ���ô˵���ﻹ��Ҫ���̵߳ı���??
unsigned int WINAPI DirectoryChangeHandler::WorkThreadProc(LPVOID arg)
{
	DirectoryChangeHandler * pf = (DirectoryChangeHandler *)arg;
	DWORD last_wait, wait_t = pf->NextWaitTime();
	last_wait = wait_t;
	while(pf->m_running)
	{
		//pf->m_nHandles �������0, ���򷵻� WAIT_FAILED
		DWORD rc = ::WaitForMultipleObjectsEx(pf->m_nHandles, pf->m_changeHandles, false, wait_t, true);
		wait_t = last_wait; //�±�Ӧ��һ�����һ��notify�Ĵ���ȴ��������
		if(rc == WAIT_IO_COMPLETION)	//���첽����
		{
			//cout << "WAIT_IO_COMPLETION called" << endl;
			continue;
		}
		if(rc == WAIT_FAILED)
		{
			cout << "wait failed, err code:" << GetLastError() << endl;
			continue;
		}
		if(rc == WAIT_TIMEOUT) {
			pf->handle_timeout();	//�ȴ���ʱ�䵽�ˣ�ͳһ����
		} else{
			rc -= WAIT_OBJECT_0;
			if(rc == 0) {//note self
				dlog("some message notify comed");
			}
			else
				pf->handle_notify(rc);
		}
		wait_t = pf->NextWaitTime();
		//cout << "wait time:" << wait_t << endl;
		last_wait = wait_t;
	}
	return 0;
}

BOOL DirectoryChangeHandler::AddDirectory(DirectoryMonitor * monitor)
{
	//XXX not thread safe
	if (!m_hThread)
	{
		Init();
	}
	::QueueUserAPC(DirectoryChangeHandler::AddDirectoryProc, m_hThread, (ULONG_PTR)monitor);
	return TRUE;
}

struct _monitor_info{
	DirectoryChangeHandler * m_dc;
	int m_id;
	string m_type;
	_monitor_info(DirectoryChangeHandler * dc, int id, string type):m_dc(dc), m_id(id), m_type(type){ };
};

//ȥ����ĳ��Ŀ¼�ļ��
BOOL DirectoryChangeHandler::DelDirectory(DirectoryMonitor * monitor)
{
	if (!m_hThread)
		return FALSE;
	
	dlog("DirectoryChangeHandler::DelDirectory before APC");
	::QueueUserAPC(DirectoryChangeHandler::DelDirectoryProc, m_hThread,
				   (ULONG_PTR)new _monitor_info(monitor->m_dc, monitor->m_id, monitor->m_type));
	return TRUE;
}

//����ȫ�����
BOOL DirectoryChangeHandler::Terminate()
{
	if (m_hThread)
	{
		::QueueUserAPC(DirectoryChangeHandler::TerminateProc, m_hThread, (ULONG_PTR)this);
		::WaitForSingleObjectEx(m_hThread, 60000, true);	//10s ���ᳬʱ�����·�����Ч��ַ�������ˣ�
		::CloseHandle(m_hThread);
		::CloseHandle(m_event);
		dlog("in DirectoryChangeHandler Terminate(), not delete this!");

		m_hThread = NULL;
		m_dwThreadId = 0;
	}
	return TRUE;
}

void CALLBACK DirectoryChangeHandler::AddDirectoryProc(__in  ULONG_PTR arg)
{
	dlog("in DirectoryChangeHandler::AddDirectoryProc", true);
	DirectoryMonitor * monitor = (DirectoryMonitor *) arg;
	DirectoryChangeHandler * dc = monitor->m_dc;
	monitor->m_id = dc->_id++;
	//�������monitor
	dc->m_monitors[monitor->m_id] = monitor;
	map<string, CReadDirectoryChanges*>::iterator it = dc->m_changes.find(monitor->m_type);
	CReadDirectoryChanges * changes = NULL;
	if(it == dc->m_changes.end())
	{
		if(dc->m_nHandles >= MAXIMUM_WAIT_OBJECTS)
			return ;
		changes = dc->m_changes[monitor->m_type] = new CReadDirectoryChanges();
		dc->m_changeHandles[dc->m_nHandles++] = changes->GetWaitHandle();
		dc->m_changeArr[dc->m_nHandles-1] = changes;
	}
	else
		changes = it->second;
	//����Ŀ¼����
	changes->AddDirectory(	monitor->m_homew.c_str(), 
							monitor->m_id,
							TRUE, ALL_NOTIFY_CHANGE_FLAGS);
}

void CALLBACK DirectoryChangeHandler::DelDirectoryProc(__in  ULONG_PTR arg)
{
	//������첽�ģ�����monitor�Ѿ��ͷ��ˣ����Ե�ַ����
	dlog("in DirectoryChangeHandler::DelDirectoryProc",true);
	_monitor_info * monitor = (_monitor_info *) arg;
	DirectoryChangeHandler * dc = monitor->m_dc;
	map<int, DirectoryMonitor*>::iterator iter = dc->m_monitors.find(monitor->m_id);
	CReadDirectoryChanges * changes = NULL;
	if(iter != dc->m_monitors.end())
	{
		changes = dc->m_changes[monitor->m_type];
		int reqnum = changes->GetRequestNum();
		//HANDLE hd = changes->GetWaitHandle();
		bool get = changes->DelDirectory(monitor->m_id);
		dlog("dc->m_monitors erase DirectoryMonitor");
		dc->m_monitors.erase(iter);
		//û��ɾ�� m_changeHandles �� m_changeArr
		//Ҳ����˵ռ��wait_multi��һ��λ�ã����changesΪ����
		if(reqnum == 1 && get)
		{//Ҳ��changesָ�뱻�ͷ��ˣ�û�����ü�����������ֻ���Ƴ�����ָ���֪ͨռ��λ��
			//������������
			dc->m_changes.erase(monitor->m_type);
			for(unsigned int i=0; i < dc->m_nHandles; i++)
			{
				if(dc->m_changeArr[i] == changes)
				{
					dlog("del changes in m_changeArr");
					//swap
					dc->m_changeArr[i]		= dc->m_changeArr[dc->m_nHandles-1];
					dc->m_changeHandles[i]	= dc->m_changeHandles[dc->m_nHandles-1];
					dc->m_nHandles--;
					break;
				}
			}
		}
	}
	delete monitor;
}

void CALLBACK DirectoryChangeHandler::TerminateProc(__in  ULONG_PTR arg)
{
	dlog("in DirectoryChangeHandler::TerminateProc",true);
	DirectoryChangeHandler * phd = (DirectoryChangeHandler *)arg;
	//XXX: here has a bug! err phd if handle later than delete it
	for(map<string, CReadDirectoryChanges*>::iterator iter = phd->m_changes.begin(); 
		iter != phd->m_changes.end(); ++iter)
			iter->second->Terminate();
	phd->m_changes.clear();
	phd->m_monitors.clear();
	phd->m_running = false;
}

void DirectoryChangeHandler::handle_timeout()
{
	dlog("in DirectoryChangeHandler::handle_timeout",true);
	int cnt = 0;
	for (map<int, DirectoryMonitor*>::iterator it = m_monitors.begin();
		 it != m_monitors.end(); ++it)
	{
		cnt += it->second->SendNotify();
		it->second->ClearBlacklist();	//���������
		if(m_waittime == MAX_SLEEP_TIME)
			it->second->release_resource();
	}
	//������timeout��Ҫ����ģ���ô������timeout�����û�в���timeout
	m_waittime = cnt > 0 ? m_def_waittime : MAX_SLEEP_TIME;
}

void DirectoryChangeHandler::handle_notify(int index)
{
	dlog("in DirectoryChangeHandler::handle_notify", true);
	CReadDirectoryChanges * changes = m_changeArr[index];
	list<TDirectoryChangeNotification> li;
	//�Ӹ��Ե�֪ͨ�б���ȡ������֪ͨ
	changes->PopAll(li);

	typedef list<TDirectoryChangeNotification>::iterator iter_t;
	typedef map<int, DirectoryMonitor*>::iterator map_iter_t;
	map_iter_t mapit, endit = m_monitors.end();
	//cout << "monitors.size=" << m_monitors.size()<< endl;
	if(!m_monitors.empty())
	{
		int last_id = -1;
		DirectoryMonitor* mon = NULL;
		for(iter_t it = li.begin(); it != li.end(); ++it)
		{
			if(last_id != it->id)
			{
				mapit = m_monitors.find(it->id);
				if(mapit != endit)
				{
					last_id = it->id;
					mon = mapit->second;
					mon->GetNotify(*it);
				}
			}
			else
			{
				mon->GetNotify(*it);
			}
		}
	}
	m_waittime = m_def_waittime;
}
