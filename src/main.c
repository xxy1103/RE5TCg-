#include "platform/platform.h"
// �����Զ���� WebSocket ͷ�ļ�����������غ����ͽṹ��
#include "websocket/websocket.h" 
// �����Զ�������ݱ�ͷ�ļ��������� DNS ������ؽṹ��ͺ���
#include "websocket/datagram.h" 
#include "websocket/dnsServer.h" // ���� DNS ��������غ�������

#include <stdio.h>    // ������׼�������ͷ�ļ�
#include <time.h>     // ����ʱ����ص�ͷ�ļ��������������������
#include <string.h>   // �����ַ���������
#include <stdlib.h>   // �����ڴ���亯��
#include "debug/debug.h"   // ����������ص�ͷ�ļ�

/**
 * @brief ��������ڵ㡣
 * 
 * @return int �ɹ����� 0��ʧ�ܷ��� 1��
 */
int main() {    
    // ��ʼ����־�ļ�
    init_log_file();
    // ������־��¼����Ϊ DEBUG �Բ鿴������Ϣ
    set_log_level(LOG_LEVEL_INFO);
    
    log_info("�����������߳�DNS���������...");
    log_info("���汾���ԣ�");
    log_info("  - ���̲߳��д���");
    log_info("  - I/O��������");
    log_info("  - �̰߳�ȫ��IDӳ��");
    log_info("  - �߲�������");

    // ��ʼ��ƽ̨��Դ
    platform_init();

    // �������߳�DNS���������
    if (start_dns_proxy_server_threaded() != MYSUCCESS) {
        log_error("���߳�DNS�������������ʧ��");
        platform_cleanup(); // �����ѳ�ʼ������Դ
        return 1; // ����ʧ�ܣ������˳�
    }
    
    // ����ƽ̨��Դ
    platform_cleanup();
    
    // ������־�ļ�
    cleanup_log_file();
    
    log_info("���߳�DNS����������������˳�");
    return 0; // ������������
}
