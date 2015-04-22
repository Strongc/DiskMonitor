#ifndef DIRECTORY_MONITOR_H
#define DIRECTORY_MONITOR_H

#include<vector>
#include<map>
using namespace std;
#include "MonitorUtil.h"

//class DirectoryChangeHandler;
struct TDirectoryChangeNotification;

/*
 *	�������ü��Ŀ¼����, ÿһ��Ŀ¼һ��ʵ��������ѡ�����ε��ļ����У�
 *	DirectoryChangeHandler �����̻߳�ص����ﺯ��������Ӧ��Ŀ¼�仯
 */
class DirectoryMonitor
{
	friend class DirectoryChangeHandler;

public:
	DirectoryMonitor(DirectoryChangeHandler * dc, const string & path, const string& type,
					 void (*cbp)(void *arg, LocalNotification *), void * varg);
	~DirectoryMonitor();

	int State() const { return m_running;}

	//����֪ͨ��Ϣ�Ķ�������Ҫ����listupdate/tree�󱾵صĸ���
	//@retrun: �ɹ�����0�� ʧ�ܷ��ش�����
	//@param: act = 0 ɾ����1 �����ļ��� 2 �����ļ��У� 3 �ƶ��� 4 ������ 5 ������
	//@param: LPtrCancel ����ȡ�����������flag�� ��ʼΪ0������Ϊ1ȡ������ (��ʱδʵ��)
	//@param: from ���Զ��ļ�������ѭSHFileOperation�������ļ�����( ����Ŀǰ��һ����������ʵ�ְ�)
	DWORD DoActWithoutNotify(int act, const string & from, const string & to=string(), DWORD flag=0, int * LPtrCancel=NULL);
	//��blacklist��ʵ������
	DWORD DoActWithoutNotify2(int act, const string & from, const string & to=string(), DWORD flag=0, int * LPtrCancel=NULL);

private:
	void Pause()	{};//{m_running = 0;}
	void Resume()	{};//{m_running = 1;}

private:
	//���������أ����ڻ�û���õ���Ҳ���������û���������Ŀ¼��
	int Terminate();
	int GetNotify(struct TDirectoryChangeNotification & notify);
	int SendNotify(); //���ط��͵�����
	bool filt_notify2(notification_t & notify);
	bool guess(notification_t & notify);
	bool filt_old_notify(notification_t & filter, int advance);
	void release_resource();
	void UpdateAttributeCache(const wstring & rpathw);
	DWORD GetAttributeFromCache(const wstring & rpathw, DWORD act, DWORD & self);

	int ClearBlacklist();
private:
	DirectoryChangeHandler * m_dc;
	string	m_home;
	wstring m_homew;
	wstring m_silent_dir;	//������ϢĿ¼
	string	m_type;							//����������������
	void (*m_cbp)(void *, LocalNotification *);
	void * m_varg;

	int m_id;					//inner id
	volatile int m_running;		//run flag
	

	//֪ͨ���˲���
	vector<notification_t>	m_notifications;	//�ݴ��֪ͨ���������˺�ͳһ����
	map<wstring, DWORD>		m_file_attrs;		//�ļ�Ŀ¼��attr�������������1
	wstring m_expert_path;	//������һ��֪ͨ��·��
	WORD m_expert_act;	//������һ������
	int m_guess_cnt;	//���ϴβ������ļ���
	class NotificationBlacklist * m_blacklist;	//������Ϣ������
	//notification_t m_boss;	//�ϴ�֪ͨ��������	??
	//debug
	bool m_showfilt;
	bool m_isXP;	//xpϵͳ������Щ��ͬ
};

#endif
