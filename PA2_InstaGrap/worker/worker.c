#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "protocol.h"
#include "worker.h"

int file_count = 0;

int _send(int sock, char *message, int use_sock) {
	DPRINT(printf("> _send | sock: %d\n", sock));
	DPRINT(printf("> message\n%s\n> message end\n", message));

	int len = 0, s = 0, result  = 0;
	char *orig = message;

	len = strlen(orig);
	if (use_sock) {
		while (len > 0 && (s = send(sock, orig, len, 0)) > 0) {
			orig += s;
			len -= s;
			result += s;
		}
	}
	else {
		while (s < len)
			s += write(sock, message + s, len - s);
	}

	DPRINT(printf("< _send\n"));
	return result;
}

int _recv(int sock, char **message, int use_sock) {
	DPRINT(printf("> _recv | sock: %d\n", sock));
	int len = 0, s = 0;
	char buf[BUF_SIZE];
	
	if (*message != NULL) {
		FREE(*message);
	}

	if (use_sock) {
		while ((s = recv(sock, buf, BUF_SIZE - 1, 0)) > 0) {
			buf[s] = 0x0;
			if (*message == NULL) {
				*message = (char *)malloc(sizeof(char) * s);
				memcpy(*message, buf, s);
				len = s;
			}
			else {
				*message = realloc(*message, len + s + 1);
				strncpy(*message + len, buf, s);
				(*message)[len + s] = 0x0;
				len += s;
			}
		}
	}
	else {
		while ((s = read(sock, buf, BUF_SIZE - 1)) > 0) {
			buf[s] = 0x0;
			if (*message == NULL) {
				*message = (char *)malloc(sizeof(char) * s);
				memcpy(*message, buf, s);
				(*message)[s] = 0x0;
				len = s;
			}
			else {
				*message = realloc(*message, len + s + 1);
				strncpy(*message + len, buf, s);
				(*message)[len + s] = 0x0;
				len += s;
			}
		}
	}

	if (len <= 0) {
		fputs("Failed to receive message.\n", stderr);
		exit(EXIT_FAILURE);
	}

	DPRINT(printf("< recv message\n%s\n< recv message end\n", *message));
	DPRINT(printf("< _recv | %d\n", len));
	return len;
}

char * concat(const char *p, int n, const char *s) {
	DPRINT(printf("> concat\n"));
	char *r = (char *)malloc(sizeof(char) * 20);
	if (p == NULL)
		sprintf(r, "%d%s", n, s);
	else
		sprintf(r, "%s%d%s", p, n, s);
	r = (char *)realloc(r, sizeof(char) * strlen(r));
	
	DPRINT(printf("< concat %s\n", r));
	return r;
}

int open_server(int port, struct sockaddr_in *address) {
	DPRINT(printf("> open_server | port = %d\n", port));

	int sock = 0;
	int addrlen = sizeof(*address);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		perror("socket failed: ");
		exit(EXIT_FAILURE);
	}

	memset(address, 0, sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_addr.s_addr = INADDR_ANY;
	address->sin_port = htons(port);
	
	if (bind(sock, (struct sockaddr *)address, sizeof(*address)) < 0) {
		perror("bind failed: ");
		exit(EXIT_FAILURE);
	}

	DPRINT(printf("< open_server\n"));
	return sock;
}

int task(int sock, pthread_mutex_t *m) {
	DPRINT(printf("> task | sock = %d\n", sock));
	
	int status = 0, i = 0, len = 0, my_file_count = 0, pipes1[2] = { -1, }, pipes2[2] = { -1, };
	char *message = NULL, *c_content = NULL, *testcase = NULL, *filename = NULL, *output = NULL;
	union _type_caster caster;
	FILE *fp = NULL;
	pid_t pid = 0;

	// Receive C file content and testcase
	len = _recv(sock, &message, 1);
	shutdown(sock, SHUT_RD);
	
	printf("len: %d ", len);
	// Get C file length and copy content
	for (i = 0; i < 4; i++)
		caster.data[i] = message[i];

	for (i = 0; i < caster.length; i++) {
		printf("%d ", message[i]);
		if ((i + 1) % 8 == 0)
			printf("\n");
	}
	
	c_content = (char *)malloc(sizeof(char) * (caster.length + 1));
	strncpy(c_content, message + 4, caster.length);
	c_content[caster.length] = 0x0;

	// Get testcase
	testcase = (char *)malloc(sizeof(char) * (len - strlen(c_content) - 4 + 1));
	strcpy(testcase, message + strlen(c_content) + 4);
	testcase[len - strlen(c_content) - 4] = 0x0;
	FREE(message);

	DPRINT(printf(": C code\n%s\n: C code end\n: Testcase\n%s\n: Testcase end\n", c_content, testcase));

	// Get filename and write C content to file
	pthread_mutex_lock(m);
	my_file_count = file_count;
	file_count++;
	pthread_mutex_unlock(m);

	filename = concat("/tmp/", my_file_count, ".c");
	output = concat("/tmp/", my_file_count, ".out");
	fp = fopen(filename, "w");
	fputs(c_content, fp);
	fclose(fp);
	FREE(c_content);

	pipe(pipes1);
	pid = fork();
	if (pid == -1) {
		fprintf(stderr, "fork error\n");
		exit(EXIT_FAILURE);
	}
	else if (pid == 0) {
		// Child process
		close(pipes1[READ_END]); // Close read pipe
		dup2(pipes1[WRITE_END], 2);
		execlp("gcc", "gcc", filename, "-o", output, NULL);
	}
	else {
		// Parent process
		close(pipes1[WRITE_END]); // Close write pipe
		wait(&status);
		DPRINT(printf("gcc return value: %d\n", status));
		if (status != 0) {
			// Build failed
			_recv(pipes1[READ_END], &c_content, 0);
			status = BUILD_FAILED;
			message = concat(NULL, status, c_content);
			message[0] -= '0';
			_send(sock, message, TRUE);
			shutdown(sock, SHUT_WR);
			FREE(message);
			execlp("rm", "rm", filename, NULL);
		}
		close(pipes1[READ_END]);
	}

	// Build success
	pipe(pipes1);
	pipe(pipes2);
	
	pid = fork();
	if (pid == -1) {
		fprintf(stderr, "fork error\n");
		exit(EXIT_FAILURE);
	}
	else if (pid == 0) {
		// Child process
		close(pipes1[READ_END]); // Close read pipe
		close(pipes2[WRITE_END]); // Close write pipe
		
		dup2(pipes1[WRITE_END], 1);
		dup2(pipes2[READ_END], 0);

		execl(output, output, NULL);
	}
	else {
		// Parent process
		close(pipes1[WRITE_END]); // Close write pipe
		close(pipes2[READ_END]); // Close read pipe

		_send(pipes2[WRITE_END], testcase, FALSE);
		_recv(pipes1[READ_END], &c_content, FALSE);
		status = BUILD_SUCCESS;
		message = concat(NULL, status, c_content);
		message[0] -= '0';
		_send(sock, message, TRUE);
		shutdown(sock, SHUT_WR);
		FREE(message);
		execlp("rm", "rm", filename, output, NULL);
	}
	DPRINT(printf("< task\n"));
}