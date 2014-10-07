#ifndef _SMTP_H
#define	_SMTP_H

struct smtp;
struct smtp *smtp_create(const char *server, const char *port);
int smtp_free(struct smtp *self);

int smtp_send(struct smtp *self, const char *usr, const char *pwd, const char *subject, const char *content, const char *to);


#endif // !_SMTP_H
