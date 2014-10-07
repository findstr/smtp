#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include "base64.h"
#include "smtp.h"

struct smtp {
	char			domain[64];	
	struct sockaddr_in	addr;
};

static int rcv_line(SOCKET s, char *buff, int len)
{
	int curr;

	assert(buff);

	memset(buff, 0, len);

	do {
		curr = recv(s, buff, len, 0);
	} while (curr < 2 || buff[curr - 2] != '\r' && buff[curr - 1] != '\n');

	return curr;
}

static int snd_line(SOCKET s, const char *buff, int len)
{
	int once;
	
	assert(buff);

	while (len) {
		once = send(s, buff, len, 0);

		len -= once;
		buff += once;
	}

	return 0;
}

static SOCKET smtp_connect(struct smtp*self, int retry)
{
	int err;
	SOCKET s;
	char buff[1024];

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET)
		return s;

	err = connect(s, (struct sockaddr *)&self->addr, sizeof(self->addr));
	if (err < 0)
		return INVALID_SOCKET;
	
	rcv_line(s, buff, 1024);

	if (strstr(buff, "220") == 0)
		return INVALID_SOCKET;

	return s;
}

static int smtp_discon(SOCKET s)
{
	char buff[128];

	assert(s != INVALID_SOCKET);

	strncpy(buff, "QUIT\r\n", _ARRAYSIZE(buff));
	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, _ARRAYSIZE(buff));

	closesocket(s);

	return 0;
}

static int smtp_hello(SOCKET s)
{
	char buff[128];

	sprintf(buff, "HELO %d \r\n", rand() % 1024);

	snd_line(s, buff, strlen(buff));

	rcv_line(s, buff, _ARRAYSIZE(buff));

	if (strstr(buff, "250") == NULL)
		return -1;

	return 0;
}

static int smtp_login(SOCKET s, const char *usr, const char *pwd)
{
	char buff[128];

	assert(usr);
	assert(pwd);

	//AUTH LOGIN
	sprintf(buff, "AUTH LOGIN\r\n");
	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, _ARRAYSIZE(buff));
	if (strstr(buff, "334") == NULL)
		return -1;

	//USR
	memset(buff, 0, sizeof(buff));
	base64(buff, usr, strlen(usr) * sizeof(usr[0]));
	strncat(buff, "\r\n", _ARRAYSIZE(buff));

	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, _ARRAYSIZE(buff));
	if (strstr(buff, "334") == NULL)
		return -1;

	//PWD
	memset(buff, 0, sizeof(buff));
	base64(buff, pwd, strlen(pwd) * sizeof(pwd[0]));
	strncat(buff, "\r\n", _ARRAYSIZE(buff));

	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, ARRAYSIZE(buff));
	if (strstr(buff, "235") == NULL)
		return -1;

	return 0;
}

static int smtp_from(SOCKET s, const char *domain, const char *usr)
{
	char buff[128];

	assert(domain);
	assert(usr);

	sprintf(buff, "MAIL FROM: <%s@%s>\r\n", usr, domain);

	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, _ARRAYSIZE(buff));
	
	if (strstr(buff, "250") == NULL)
		return -1;

	return 0;
}

static int smtp_to(SOCKET s, const char *to)
{
	char buff[128];

	assert(to);

	sprintf(buff, "RCPT TO:<%s>\r\n", to);
	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, _ARRAYSIZE(buff));

	if (strstr(buff, "250") == NULL)
		return -1;

	return 0;
}

static int smtp_data_begin(SOCKET s, const char *usr, const char *domain, const char *subject, const char *to)
{
	char buff[128];

	assert(usr);
	assert(domain);
	assert(subject);
	assert(to);

	strncpy(buff, "DATA\r\n", _ARRAYSIZE(buff));
	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, _ARRAYSIZE(buff));
	
	if (strstr(buff, "354") == NULL)
		return -1;

	sprintf(buff,	"From: <%s@%s>\r\nTo:<%s>\r\nSubject:%s\r\n\r\n", usr, domain, to, subject);
	snd_line(s, buff, strlen(buff));

	return 0;
}

static int smtp_data(SOCKET s, const char *buff, unsigned long len)
{
	assert(buff);

	snd_line(s, buff, strlen(buff));

	return 0;
}

static int smtp_data_end(SOCKET s)
{
	char buff[128];

	strncpy(buff, "\r\n.\r\n", _ARRAYSIZE(buff));

	snd_line(s, buff, strlen(buff));
	rcv_line(s, buff, _ARRAYSIZE(buff));

	if (strstr(buff, "250") == NULL)
		return -1;

	return 0;
}

struct smtp *smtp_create(const char *server, const char *port)
{
	WSADATA	ws;
	hostent	*host;
	struct smtp*mail;
	char server_name[64];

	WSAStartup(MAKEWORD(2, 2), &ws);

	server_name[0] = 0;
	strncat(server_name, "smtp.", _ARRAYSIZE(server_name));
	strncat(server_name, server, _ARRAYSIZE(server_name));

	host = gethostbyname(server_name);
	if (host == NULL)
		return NULL;

	mail = (struct smtp *)malloc(sizeof(*mail));
	if (mail == NULL)
		return mail;

	memset(mail, 0, sizeof(*mail));

	mail->addr.sin_family = AF_INET;
	mail->addr.sin_addr.s_addr = *(u_long *) host->h_addr_list[0];
	mail->addr.sin_port = htons((u_short)strtoul(port, NULL, 10));

	srand((unsigned int)time(NULL));

	//TODO:
	strncpy(mail->domain, server, _ARRAYSIZE(mail->domain));

	return mail;
}

int smtp_free(struct smtp *self)
{
	assert(self);
	
	free(self);

	WSACleanup();

	return 0;
}

int smtp_send(struct smtp *self, const char *usr, const char *pwd, const char *subject, const char *content, const char *to)
{
	int err;
	SOCKET s;

	s = smtp_connect(self, 3);
	if (s == INVALID_SOCKET)
		return -1;

	err = smtp_hello(s);
	if (err < 0)
		return err;

	err = smtp_login(s, usr, pwd);
	if (err < 0)
		return err;

	err = smtp_from(s, self->domain, usr);
	if (err < 0)
		return err;

	err = smtp_to(s, to);
	if (err < 0)
		return err;

	err = smtp_data_begin(s, usr, self->domain, subject, to);
	if (err < 0)
		return err;

	err = smtp_data(s, content, strlen(content));
	if (err < 0)
		return err;

	err = smtp_data_end(s);
	if (err < 0)
		return err;

	smtp_discon(s);

	return 0;
}