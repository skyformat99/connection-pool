/*
*author:sunwake(sunwake at foxmail dot com)
*date:2016-10-31
*desc:
*this is a connection pool for msg_mail connect. 
*it could auto increases or decreases conn number,
*just like a tomcat`s connection pool 
*/
#include <stdlib>// for multimap?
#include <pthread.h>// for lock
#include "msg_mail.h"
#include "config.h"
/*============================================================
			 minIdleN 
			  |----|
|_____|_______|____|___|____|__________|    connection num
0    initN   useN     realN             maxN
			  |-IdleN -|
			  |--maxIdleN---|


initN:��������ʼ������������û�����ӵ�ʱ��realN���ܻ�������Ŀ��
useN������������ʹ�õ������������п���Ϊ0
minIdleN����������С���������������κ�ʱ�� �����ٱ���
		minIdleN���������ӣ�minIdleN<initN��
maxIdleN:�����������������������������������ʱ����ʼ�޳�����Ŀ������ӣ�����UseN+maxIdleN�Ĳ��֣���
idleN�����������е��������������ӳ��κ�ʱ�򶼱���һ�������Ŀ������ӣ�
		�Ա���ʱ֮�衣��minIdleN< idleN<maxIdleN��
realN��������ʵ������������realN = UseN + idleN��
maxN�����������������������������������ṩ���ӡ�
balanceIdleN:= (minIdleN+maxIdleN)/2 (���Ҷ��������)����minIdleN+(maxIdleN-minIdleN��*0.8����

 �C maxNֱ�Ӹ�������realN�У�����minIdleN �C maxIdleN�����Ŀ���������ʱ������
���ݿ����ӳ������ӵ������Ƕ�̬�����ģ�������������realN����Զ��minIdleN
���ӳ�������������̬�����Ĺ���
��	���ӳ��п�������idleN������С��minIdleNʱ���Զ�������balanceIdleN�����ӡ����ڼ���̺߳͹����߳��е�����
��	���ӳ��п�������idleN����������maxIdleNʱ���Զ�������balanceIdleN�����ӡ����ڼ���߳��е�����
��	���ӳ��п�������idle connection������minIdleN<idleN<maxIdleN�������в���idle connection��ʱʱ��> timeout��timeout���Լ��趨�ġ������رճ�ʱ�ǲ���idle connection���ڱ�֤idleN >=minIdleN������� ��
��	����idle connection��<minIdleN�Ĳ��ֲ����Ѿ���ʱ�����ӣ����á������㷨����
��	ÿmax_idle_time����һ�Σ�����̣߳�����Ĭ��15s��like tomcat��


==================================================================*/
#define CONF_DEF_POOL_TIMEOUT		45//45s		
#define CONF_DEF_POOL_INIT_NUM		3		
#define CONF_DEF_POOL_MAX_NUM		8// the max speed of anti fire_wall is 6 files per second
#define CONF_DEF_POOL_MIN_IDLE_NUM	2
#define CONF_DEF_POOL_MAX_IDLE_NUM	4
#define CONF_DEF_POOL_MAX_IDLE_TIME	15// 15s 


#define CONF_POOL_TIMEOUT_STR		connection_timeout
#define CONF_POOL_INIT_NUM_STR		connection_pool_init_num
#define CONF_POOL_MAX_NUM_STR		connection_pool_max_num
#define CONF_POOL_MIN_IDLE_NUM_STR	connection_pool_min_idle_num
#define CONF_POOL_MAX_IDLE_NUM_STR	connection_pool_max_idle_num
#define CONF_POOL_MAX_IDLE_TIME_STR	connection_pool_max_idle_time

using namespace std;

typedef struct Running_info{
	unsigned int using_num;
	unsigned int idle_num;
	unsigned int total_num;
}pool_running_info_t;
typedef struct Config_info{
	unsigned int timeout;
	unsigned int init_num;
	unsigned int min_idle_num;
	unsigned int max_idle_num;
	unsigned int balance_ilde_num;
	unsigned int max_num;
	unsigned int max_idle_time;
}pool_config_info_t;

class conn_pool{
public:
	static conn_pool* get_instance(Config& myconf);
	bool init_conn_pool();
	msg_mail& get_conn();
	bool release_conn(msg_mail& mail);
	// for count 
	bool get_running_info(pool_running_info_t& info);
	bool get_config_info(pool_config_info_t& info);
	// this func is for a loop thread which keep conn_pool in a health station
	void* conn_pool_keep_balance(void* );
private:
	conn_pool();// no constructer with 0 param
	conn_pool(Config& myconf);
	~conn_pool();
	conn_pool(const conn_pool& conn);// no copy constructer
	conn_pool& operator = (const conn_pool& conn);// no sign constructer
	void wake_check_thread(){is_interrupt = true;}
	inline unsigned int add_useN(unsigned int n);
	inline unsigned int sub_useN(unsigned int n);
	inline unsigned int add_idleN(unsigned int n);
	inline unsigned int sub_idleN(unsigned int n);
	inline unsigned int add_realN(unsigned int n);
	inline unsigned int sub_realN(unsigned int n);
private:
	int auto_adjust_conns();
	// increase n conn  when idleN < m_minIdleN
	int increase_conns(unsigned int n);
	// close over number connections and timeout connections
	// although this opposite design rule ,we do this major to do effect
	int close_over_and_timeout_conns(unsigned int n,unsigned int start_time);
	////close the connections that is behind position of minIdle and out of time
	//int close_conn_timeout(long start_time);
	
	msg_mail* create_conn();
	bool  close_conn(msg_mail* conn);
	long get_time_insecond();
	bool destroy_conn_pool();
	
	volatile bool is_interrupt;// is interrupt by another thread
	pthread_mutex_t m_mutex;//thread locker
	static conn_pool* m_instance;
	volatile bool m_is_destruct;
	pthread_t m_keep_balance_threadId;
	
	volatile unsigned int m_useN;// atomic_t?
	volatile unsigned int m_idleN;
	volatile unsigned int m_realN;
	
	
	unsigned int m_timeout;
	unsigned int m_initN;
	unsigned int m_minIdleN;
	unsigned int m_maxIdleN;
	unsigned int m_balanceIdleN;// balance idle connection number to avoid create or free conn frequently
	unsigned int m_maxN;
	unsigned int m_max_idle_time;
	
	
	multimap<unsigned long,msg_mail*> m_map_idle_conns;
	typedef multimap<unsigned long,msg_mail*> map_conns_t;
	//typedef multimap<unsigned long,msg_mail*>::reverse_iterator rmap_it_t;
	typedef multimap<unsigned long,msg_mail*>::iterator map_it_t;
	//typedef multimap<unsigned long,msg_mail*>::iterator map_rit_t;
	typedef multimap<unsigned long,msg_mail*>::reverse_iterator map_rit_t;

	
}