/* Copyright (c) 20016-2025, SnailGame Inc.
* All rights reserved.

* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1.Redistributions of source code must retain the above copyright notice, this
*   list of conditions and the following disclaimer.
* 2.Redistributions in binary form must reproduce the above copyright notice,
*   this list of conditions and the following disclaimer in the documentation
*   and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* The views and conclusions contained in the software and documentation are those
* of the authors and should not be interpreted as representing official policies,
* either expressed or implied, of the FreeBSD Project.
*/

#include "sdns.h"

#define TYPE_A      1   /* a host address */
#define TYPE_CNAME  5   /* the canonical name for an alias */

#define RETRANS_INTERVAL    2
#define RETRANS_TRY_NUM     2

#define DNS_GET16(num) ((((uint16_t)(num))>>8) | ((uint16_t)((num)<<8)))
#define DNS_GET32(num) ((num >> 24)|((num >>8)&0x0000ff00)|((num << 8)&0x00ff0000)|(num << 24));

/* Added by xie hui */
  char *public_dnsserver = NULL;
  unsigned int public_dnsport = 53;
/* End added by xie hui */
//用于dns解析的结构体
typedef struct _dns_head_info { //dns 头部
    unsigned short ID;
    unsigned short tag; // dns 标志(参数)
    unsigned short numQ; // 问题数
    unsigned short numA; // 答案数
    unsigned short numA1; // 权威答案数
    unsigned short numA2; // 附加答案数
} dns_head_type;

typedef struct _dns_query_info { //dns 查询结构
    //char name[64];
    //查询的域名,这是一个大小在0到63之间的字符串；
    unsigned short type;
    //查询类型，大约有20个不同的类型////////////////
    unsigned short classes;
    //查询类,通常是A类既查询IP地址。
} dns_query_type;

#ifdef WIN32

#pragma pack(push) //保存对齐状态
#pragma pack(2) // 2 bytes对齐
typedef struct dns_response { //DNS响应数据报：
    unsigned short type; //查询类型
    unsigned short classes; //类型码
    unsigned int ttl; //生存时间
    unsigned short length; //资源数据长度
} response;
#pragma pack(pop) //恢复对齐状态

#else
typedef struct dns_response { //DNS响应数据报：
    unsigned short type __attribute__((packed)); //查询类型
    unsigned short classes  __attribute__((packed)); //类型码
    unsigned int ttl __attribute__((packed)); //生存时间
    unsigned short length __attribute__((packed)); //资源数据长度
} response;
#endif

//域名转化
static int ch_name(const char *fname, char *tname)
{
    int j = 0;
    int i = strlen(fname) - 1;
    tname[i + 2] = 0;
    int k = i + 1;
    for (; i >= 0; i--, k--) {
        if (fname[i] == '.') {
            tname[k] = j;
            j = 0;
        }
        else {
            tname[k] = fname[i];
            j++;
        }
    }
    tname[k] = j;
    return strlen(tname) + 1;
}

//设置dns包头
static int set_dns_head(const char *name, char *buf)
{
    memset(buf, 0, sizeof(dns_head_type));

    //设置头部
    dns_head_type *dns_head = (dns_head_type *)buf;
    dns_head->ID = (unsigned short)1;
    dns_head->tag = htons(0x0100);
    dns_head->numQ = htons(1);
    dns_head->numA = 0;

    dns_query_type *dns_query = (dns_query_type *)(buf + sizeof(dns_head_type));
    int name_len = ch_name(name, (char *)dns_query);

    //设置查询信息
    dns_query = (dns_query_type *)((char *)dns_query + name_len);
    dns_query->classes = htons(1);
    dns_query->type = htons(1);
    return 1;
}

int make_dns_query_format(const char *node, char *buf, int *query_len)
{
    if (NULL == node || strlen(node) > DOMAIN_MAX_SIZE) {
        fprintf(stderr, "invalid argument node, %s\n", node);
        return -1;
    }

    set_dns_head(node, buf);
    *query_len = sizeof(dns_head_type) + sizeof(dns_query_type) + strlen(node) + 2;

    return 0;
}

int make_dns_query(char *buf, int query_len, time_t *ttl, int *Anum)
{
    struct sockaddr_in addr;
    int sockfd = -1;
    struct timeval timeout = { RETRANS_INTERVAL, 0 };
    int i, ret = -1;
    int addrlen, send_len, result_len;
    int try_num = 0;
    dns_head_type *dns_head;
#ifdef WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    addr.sin_family = AF_INET;
    //addr.sin_addr.s_addr = inet_addr(public_dnsserver);
    inet_pton(AF_INET, public_dnsserver, &(addr.sin_addr.s_addr));
    addr.sin_port = htons((uint16_t)public_dnsport);
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == sockfd) {
        fprintf(stderr, "socket error\n");
        goto clear;
    }

    ret = wait_writable(sockfd, timeout);
    if (ret != 0) {
        fprintf(stderr, "wait writable timeout\n");
        goto clear;
    }
    while (try_num++ <= RETRANS_TRY_NUM) {
        send_len = sendto(sockfd, buf, query_len, 0, (struct sockaddr*)&addr, sizeof(struct sockaddr));
        if (send_len != query_len) {
            fprintf(stderr, "sendto dns query failed\n");
            ret = -1;
            goto clear;
        }
        ret = wait_readable(sockfd, timeout);
        if (ret == 0) {
            break;
        }
    }
    if (try_num > RETRANS_TRY_NUM) {
        fprintf(stderr, "dns query failed over try num\n");
        ret = -1;
        goto clear;
    }

    addrlen = sizeof(struct sockaddr);
    result_len = recvfrom(sockfd, buf, DNS_DEFAULT_DATA_SIZE, 0/*MSG_WAITALL*/,
        (struct sockaddr *)&addr, (socklen_t*)&addrlen);
    if (result_len <= 0) {
        fprintf(stderr, "receve dns response failed\n");
        ret = -1;
        goto clear;
    }

    //只支持A记录
    dns_head = (dns_head_type *)buf;
    int off = 0;
    int num = DNS_GET16(dns_head->numA);
    for (i = 0; i < num; i++) {
        char *result_set = buf + query_len + off;
        response *rp = (response *)(result_set + 2); //2 bytes' offsets
        uint16_t type = DNS_GET16(rp->type);
        *ttl = DNS_GET32(rp->ttl);
        //解析A记录
        if (TYPE_A == type) {
            memcpy(buf + (*Anum) * 4, (char *)(rp + 1), 4);
            (*Anum)++;
            off += (2 + sizeof(response) + 4);
        }
        else if (TYPE_CNAME == type) {
            //如果是CNAME记录则直接查找下一条记录
            off += (2 + sizeof(response) + DNS_GET16(rp->length));
        }
        else {
            //其他类型不支持
            goto clear;
        }
    }

    ret = 0;

clear:
    if (sockfd != -1) {
#ifdef WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
    }

    return ret;
}
