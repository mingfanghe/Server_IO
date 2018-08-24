// Multi_Client.cpp : 定义控制台应用程序的入口点。
//客户端第一次发完消息等服务器应答，有应答后客户端再重新发送消息

#define FD_SETSIZE      1024
#include <WINSOCK2.H>
#include <WS2tcpip.h>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#include <cassert>
#include<iterator>

#pragma comment(lib,"ws2_32.lib")
using namespace std;

#define  CLOCKS_PER_SEC ((clock_t)1000)

enum packet_receive_state {
	S_READ_LEN,
	S_READ_CONTENT
};

int QPScount_idx = 0;
const clock_t begin_time = clock();
int before_total = 0;
int current_total = 0;

void QPS_Count()
{
	const double time_span = 10;
	//10s span and count
	double current_time_duration = double(clock() - begin_time) / CLOCKS_PER_SEC;
	while (current_time_duration > ((double)(QPScount_idx + 1))*time_span)
	{
		int QPScount = current_total - before_total;
		cout << QPScount_idx + 1 << "-10s      total: [" << QPScount;
		cout << "]      average(QPS): [" << (int)((double)(QPScount) / time_span) << "]" << endl << endl;
		//QPScount_idx.fetch_add(1, std::memory_order_relaxed);
		QPScount_idx++;
		before_total = current_total;
	}
}

namespace bytes_helper {
	template <class T> struct type {};

	template <typename T, class iter>
	T read(iter it, type<T>) {
		T i = T();
		//[01][02][03][04]
		int T_len = sizeof(T);
		for (int idx = 0; idx < T_len; ++idx) {
			i |= *(it + idx) << (3 - idx) * 8;
		}
		return  i;
	}

	template <typename T, class iter>
	int write(T v, iter it, int size) {
		int i = 0;
		int T_len = sizeof(T);
		for (int idx = 0; idx < T_len; ++idx) {
			*(it + idx) = v >> (3 - idx) * 8;
			++i;
		}
		return i;
	}
}


int Send_message(int client_sock)
{
	const int buf_len = 1024;
	static const char* package_content = "Hello Server !";
	static const int host_len = strlen(package_content);
	char wbuffer[buf_len] = { 0 };
	int wbuffer_write_idx = 0;
	assert(1024 >= sizeof(int));
	int wlen = bytes_helper::write<int>(host_len, wbuffer, 1024);
	assert(sizeof(int) == wlen);
	wbuffer_write_idx += wlen;
	memcpy(wbuffer + wbuffer_write_idx, package_content, host_len);
	wbuffer_write_idx += host_len;

	int sendrt = send(client_sock, wbuffer, wbuffer_write_idx, 0);
	if (sendrt == SOCKET_ERROR)
	{
		int rt = ::WSAGetLastError();
		cout << "Send fail: " << rt << endl << endl;
		return -1;
	}
}

void Create_Multi_Client(int client_amount, vector<int> &multi_client)
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKADDR_IN ServerAddr;
	memset(&ServerAddr, 0, sizeof(ServerAddr));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(8888);
	ServerAddr.sin_addr.s_addr = inet_addr("100.64.15.20");

	for (int i = 0; i < client_amount; i++)
	{
		SOCKET ClientConnetServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		//将该套接字置为非阻塞套接字
		//ioctlsocket(ClientConnetServer, FIONBIO, 0);

		int connectrt = connect(ClientConnetServer, (SOCKADDR *)&ServerAddr, sizeof(ServerAddr));
		if (connectrt == SOCKET_ERROR)
		{
			WSAEWOULDBLOCK;
			int rt = ::WSAGetLastError();
			//cout << "connect error: " << rt << endl << endl;
			closesocket(connectrt);
			closesocket(ClientConnetServer);
			i--;
			continue;
		}
		else
		{
			multi_client.push_back(ClientConnetServer);
			Send_message(ClientConnetServer);
			cout << "current connect amount: " << i << endl << endl;
		}	
	}
	
}

int Recv_messege(int client_sock)
{
	packet_receive_state s = S_READ_LEN;

	const int buf_len = 1024;
	char rbuffer[buf_len] = { 0 };
	int rbuffer_read_idx = 0;
	int rbuffer_write_idx = 0;
	int recvmessage_len;

read_again:
	int ret = recv(client_sock, rbuffer + rbuffer_write_idx, buf_len - rbuffer_write_idx, 0);
	if (ret == SOCKET_ERROR)
	{
		int rt = ::WSAGetLastError();
		cout << "recevice fail: " << rt << endl << endl;
		return -1;
	}
	rbuffer_write_idx += ret;

read_content:
	switch (s)
	{
	case S_READ_LEN:
	{
		if (rbuffer_write_idx < sizeof(int) - 1) {
			goto read_again;
		}
		recvmessage_len = bytes_helper::read<int>(rbuffer, bytes_helper::type<int>());
		rbuffer_read_idx += sizeof(int);
		s = S_READ_CONTENT;
		goto read_content;
	}
	case S_READ_CONTENT:
	{
		int wrlen = rbuffer_write_idx - rbuffer_read_idx;
		if (wrlen >= recvmessage_len)
		{
			char recv_content[buf_len] = { 0 };
			memcpy(recv_content, rbuffer + rbuffer_read_idx, recvmessage_len);
			rbuffer_read_idx += recvmessage_len;
			wrlen = rbuffer_write_idx - rbuffer_read_idx;
			//cout << "received bytes: " << recv_content << endl;

			current_total++;

			//将缓存里的第一个消息删除，把offset之前的挪到前面
			memcpy(rbuffer, rbuffer + rbuffer_read_idx, wrlen);
			rbuffer_read_idx = 0;
			rbuffer_write_idx = wrlen;
			s = S_READ_LEN;

			if (rbuffer_write_idx != 0)
			{
				goto read_content;
			}

			break;
		}
		else
		{
			goto read_again;
		}
	}
	}
}

void Add_select(vector<int> &multi_client)
{
	struct timeval timeout;
	timeout.tv_sec = 0;  //s
	timeout.tv_usec = 0;  //ms

	fd_set read_answer_set;

	double current_time_duration = 0;
	clock_t starttime = clock();

	while (current_time_duration < (double)60)
	{
		current_time_duration = double(clock() - starttime) / CLOCKS_PER_SEC;

		QPS_Count();

		//每次调用select前都要重新设置文件描述符和时间，因为事件发生后，文件描述符和时间都被内核修改
		FD_ZERO(&read_answer_set);

		//重新添加套接字
		for (int i = 0; i < multi_client.size(); i++)
		{
			FD_SET(multi_client[i], &read_answer_set);
		}

		const int max_sockfd = 0;//Windows下该参数无所谓，可设任意值

		//开始轮询处理客户端套接字
		int selectrt = select(max_sockfd, &read_answer_set, NULL, NULL, &timeout);
	
		for (int i = 0; i < multi_client.size(); i++)
		{
			int is_set = FD_ISSET(multi_client[i], &read_answer_set);
			if (is_set > 0)
			{
				if (Recv_messege(multi_client[i]) == -1)
				{
					multi_client.erase(multi_client.begin() + i);
					closesocket(multi_client[i]);
				}
				if (Send_message(multi_client[i]) == -1)
				{
					multi_client.erase(multi_client.begin() + i);
					closesocket(multi_client[i]);
				}
			}
		}
	}
}

int main()
{
	int client_amount = 12;
	vector<int> Multi_client;
	Create_Multi_Client(client_amount, Multi_client);
	Add_select(Multi_client);
	return 0;
}